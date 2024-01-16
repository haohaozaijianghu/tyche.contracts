#include <tyche.loan/tyche.loan.hpp>
#include <tyche.reward/tyche.reward.hpp>
#include <amax.token.hpp>
#include <aplink.farm/aplink.farm.hpp>

#include "safemath.hpp"
#include <utils.hpp>
// #include <eosiolib/time.hpp> 
#include <eosio/time.hpp>
#include<tyche.reward/tyche.reward.db.hpp>
#include<tyche.loan.utils.hpp>

static constexpr eosio::name active_permission{"active"_n};

namespace tychefi {
using namespace std;
using namespace wasm::safemath;

#define NOTIFY_LIQ_ACTION( item) \
     { tyche_loan::notifyliq_action act{ _self, { {_self, active_perm} } };\
	        act.send( item );}

#define NOTIFY_TRANSFER_ACTION( from, to, quants, memo) \
     { tyche_loan::notifytranfer_action act{ _self, { {_self, active_perm} } };\
	        act.send( from, to, quants, memo );}


#define ALLOT_APPLE(farm_contract, lease_id, to, quantity, memo) \
    {   aplink::farm::allot_action(farm_contract, { {_self, active_perm} }).send( \
            lease_id, to, quantity, memo );}

#define ADD_ISSUE(contract, receiver, ido_id, quantity) \
    {	custody::add_issue_action act{ contract, { {_self, active_permission} } };\
            act.send( receiver, ido_id, quantity );}

#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + to_string((int)code) + string("]] ") + msg); }

   inline int64_t get_precision(const symbol &s) {
      int64_t digit = s.precision();
      CHECK(digit >= 0 && digit <= 18, "precision digit " + std::to_string(digit) + " should be in range[0,18]");
      return calc_precision(digit);
   }

inline int64_t get_precision(const asset &a) {
   return get_precision(a.symbol);
}

//根据新打入rewards来计算新的reward_per_share
inline static int128_t calc_reward_per_share_delta( const asset& rewards, const asset& total_shares) {
   ASSERT(rewards.amount >= 0 && total_shares.amount >= 0);
   int128_t new_reward_per_share_delta = 0;
   if (rewards.amount > 0 && total_shares.amount > 0) {
      new_reward_per_share_delta = rewards.amount * HIGH_PRECISION / total_shares.amount;
   }
   return new_reward_per_share_delta;
}
// 根据用户的votes和reward_per_share_delta来计算用户的rewards
inline static asset calc_sharer_rewards(const asset& loaner_shares, const int128_t& reward_per_share_delta, const symbol& rewards_symbol) {
   ASSERT( loaner_shares.amount >= 0 && reward_per_share_delta >= 0 );
   int128_t rewards = loaner_shares.amount * reward_per_share_delta / HIGH_PRECISION;
   // rewards = rewards * get_precision(rewards_symbol)/get_precision(loaner_shares.symbol);
   CHECK( rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow" );
   return asset( (int64_t)rewards, rewards_symbol );
}

void tyche_loan::init(const name& admin, const name& lp_refueler, const name& price_oracle_contract, const bool& enabled) {
   require_auth( _self );
   _gstate.admin                    = admin;
   _gstate.lp_refueler              = lp_refueler;
   _gstate.enabled                  = enabled;
   _gstate.price_oracle_contract    = price_oracle_contract;
   _gstate.total_principal_quant    = asset(0, _gstate.loan_token.get_symbol());
   _gstate.avl_principal_quant      = asset(0, _gstate.loan_token.get_symbol());
}

/**
 * @brief send nasset tokens into nftone marketplace
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo: two formats:
 *       1) musdt: "repay:6,ETH"  //降低抵押率，获得更多的MUSDT
 *                 "liquidate:name:6,ETH"  //降低抵押率，获得更多的MUSDT
 *       2) meth:
 */
void tyche_loan::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   CHECKC(_gstate.enabled, err::PAUSED, "not effective yet");
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );

   if (from == get_self() || to != get_self()) return;
   auto token_bank = get_first_receiver();

   //MUSDT
   if( quant.symbol == _gstate.loan_token.get_symbol() && token_bank == _gstate.loan_token.get_contract() ) { 
      if( from == _gstate.lp_refueler ){
         _gstate.total_principal_quant += quant;
         _gstate.avl_principal_quant   += quant;
         return;
      }
      auto parts  = split( memo, ":" );
      if (parts[0] == TYPE_SEND_BACK ) {
         CHECKC( parts.size() == 2, err::PARAMETER_INVALID, "memo format error" );
         auto sym = symbol_from_string(parts[1]);
         _on_pay_musdt(from, sym, quant);
      } else if( parts[0] == TYPE_LIQUDATE ) {
         CHECKC( parts.size() == 3, err::PARAMETER_INVALID, "memo format error" );
         auto sym = symbol_from_string(parts[2]);
         _liquidate(from, name(parts[1]), sym, quant);
      } else {
         //CHECKC( false, err::PARAMETER_INVALID, "memo format error" );
      }
      return;
   }
   _on_add_callateral(from, token_bank, quant);
   return;
}

//赎回抵押物，用户打入MUSDT
//internal call
void tyche_loan::_on_pay_musdt( const name& from, const symbol& collateral_sym, const asset& quant ){
   
   loaner_t::tbl_t loaners(_self, _get_lower(collateral_sym).value);
   auto itr = loaners.find(from.value);
   CHECKC(itr != loaners.end(),           err::RECORD_NOT_FOUND,  "account not existed")
   CHECKC(itr->avl_principal.amount > 0,  err::OVERSIZED,         "avl_principal must positive")

   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto collateral_itr = syms.find(collateral_sym.code().raw());
   CHECKC(collateral_itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");


   //结算利息
   auto total_unpaid_interest = _get_dynamic_interest(itr->avl_principal, itr->term_settled_at, eosio::current_time_point());

   total_unpaid_interest      += itr->unpaid_interest;
   CHECKC(total_unpaid_interest<= quant, err::OVERSIZED, "must pay more than interest")
   auto principal_repay_quant = quant - total_unpaid_interest;
   auto avl_principal = itr->avl_principal;
   if(principal_repay_quant > avl_principal) {
      //打USDT给用户
      auto  return_quant = principal_repay_quant - avl_principal;
      TRANSFER( _gstate.loan_token.get_contract(), from, return_quant, TYPE_LEND + ":"+ collateral_itr->sym.get_symbol().code().to_string() );
      //TODO
      avl_principal        = asset(0, avl_principal.symbol);
   } else {
      avl_principal         -= principal_repay_quant;
   }

   syms.modify(collateral_itr, _self, [&](auto& row){
      row.avl_principal   -= avl_principal;
   });


   loaners.modify(itr, _self, [&](auto& row){
      row.avl_principal          = avl_principal;
      row.paid_interest          += total_unpaid_interest;
      row.unpaid_interest        = asset(0, total_unpaid_interest.symbol);
      row.term_settled_at        = eosio::current_time_point();
      row.term_ended_at          = eosio::current_time_point() + eosio::days(_gstate.term_interval_days);
   });
}

//external: 获得更多的MUSDT
void tyche_loan::getmoreusdt(const name& from, const symbol& callat_sym, const asset& quant){
   require_auth(from);
   CHECKC(quant.amount > 0, err::INCORRECT_AMOUNT, "amount must positive")
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto itr = syms.find(callat_sym.code().raw());
   CHECKC(itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");

   CHECKC(_gstate.avl_principal_quant >= quant, err::OVERSIZED, "principal not enough")
   CHECKC(itr->avl_principal + quant <= itr->max_principal, err::OVERSIZED, "symbol principal not enough")

   auto loaner = loaner_t::tbl_t(_self, _get_lower(callat_sym).value);
   auto loaner_itr = loaner.find(from.value);
   CHECKC(loaner_itr != loaner.end(), err::RECORD_NOT_FOUND, "account not existed");

   asset total_interest = _get_dynamic_interest(loaner_itr->avl_principal, loaner_itr->term_settled_at, eosio::current_time_point());
   // CHECKC(false, err::RATE_EXCEEDED, " " +loaner_itr->avl_principal.to_string() + " " + loaner_itr->unpaid_interest.to_string() +  " " + total_interest.to_string() +  "  " + quant.to_string() );

   auto ratio = get_callation_ratio(loaner_itr->avl_collateral_quant, loaner_itr->avl_principal + loaner_itr->unpaid_interest + total_interest + quant, itr->oracle_sym_name);
   //TODO
   // CHECKC( ratio >= itr->init_collateral_ratio, err::RATE_EXCEEDED, "callation ratio exceeded" )
   //打USDT给用户
   TRANSFER( _gstate.loan_token.get_contract(), from, quant, TYPE_LEND + ":" + itr->sym.get_symbol().code().to_string() );

   //更新用户的MUSDT
   loaner.modify(loaner_itr, _self, [&](auto& row){
      row.avl_principal    += quant;
      row.unpaid_interest  += total_interest;
      row.term_settled_at  = eosio::current_time_point();
   });

   syms.modify(itr, _self, [&](auto& row){
      row.total_principal += quant;
      row.avl_principal   += quant;
   });

   _gstate.avl_principal_quant -= quant;
}

void tyche_loan::tgetprice( const symbol& collateral_sym ){
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto sym_itr = syms.find(collateral_sym.code().raw());
   CHECKC(sym_itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");
   auto price = get_index_price( sym_itr->oracle_sym_name );

   CHECKC(false, err::SYSTEM_ERROR, "current pirce: " + asset(price, collateral_sym).to_string() );
}

void tyche_loan::tgetliqrate( const name& owner, const symbol& callat_sym )
{
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto itr = syms.find(callat_sym.code().raw());
   CHECKC(itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");

   auto loaner = loaner_t::tbl_t(_self, _get_lower(callat_sym).value);
   auto loaner_itr = loaner.find(owner.value);
   CHECKC(loaner_itr != loaner.end(), err::RECORD_NOT_FOUND, "account not existed");

   asset total_interest = _get_dynamic_interest(loaner_itr->avl_principal, loaner_itr->term_settled_at,eosio::current_time_point());

   auto ratio = get_callation_ratio(loaner_itr->avl_collateral_quant, loaner_itr->avl_principal + loaner_itr->unpaid_interest + total_interest, itr->oracle_sym_name);
   CHECKC( false, err::RATE_EXCEEDED, "callation ratio: "  + to_string(ratio));
}

//增加质押物
void tyche_loan::_on_add_callateral( const name& from, const name& token_bank, const asset& quant ){

   //ETH
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto sym_itr = syms.find(quant.symbol.code().raw());
   CHECKC(sym_itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");
   CHECKC(token_bank == sym_itr->sym.get_contract(), err::CONTRACT_MISMATCH, "symbol not supported");

   loaner_t::tbl_t loaners(_self, _get_lower(quant.symbol).value);
   auto itr = loaners.find(from.value);
   if( itr == loaners.end() ){
      loaners.emplace(_self, [&](auto& row){
         row.owner = from;
         row.cum_collateral_quant   = quant;
         row.avl_collateral_quant   = quant;
         row.avl_principal          = asset(0, _gstate.loan_token.get_symbol());
         row.created_at             = eosio::current_time_point();
         row.term_started_at        = eosio::current_time_point();
         row.term_settled_at        = eosio::current_time_point();
         row.term_ended_at          = eosio::current_time_point() + eosio::days(_gstate.term_interval_days);
         row.unpaid_interest        = asset(0, _gstate.loan_token.get_symbol());
         row.paid_interest          = asset(0, _gstate.loan_token.get_symbol());
      });
   } else {
      loaners.modify(itr, _self, [&](auto& row){
         row.cum_collateral_quant += quant;
         row.avl_collateral_quant += quant;
      });
   }

   syms.modify(sym_itr, _self, [&](auto& row){
      row.total_collateral_quant += quant;
      row.avl_collateral_quant   += quant;
   });

}

//赎回质押物，就是减少用户的质押物
void tyche_loan::onsubcallat( const name& from, const asset& quant ) {
   require_auth(from);
   loaner_t::tbl_t loaners(_self, _get_lower(quant.symbol).value);
   auto itr = loaners.find(from.value);
   CHECKC(itr != loaners.end(),           err::RECORD_NOT_FOUND,  "account not existed")
   CHECKC(itr->avl_collateral_quant.amount > 0,  err::OVERSIZED,  "avl_principal must positive")
   CHECKC(itr->avl_collateral_quant.amount >= quant.amount, err::OVERSIZED,  "too many callat sub")

   auto remain_collateral_quant = itr->avl_collateral_quant - quant;
   //算新的抵押率

   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto sym_itr = syms.find(quant.symbol.code().raw());
   CHECKC(sym_itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");
   auto ratio = get_callation_ratio(remain_collateral_quant, itr->avl_principal, sym_itr->oracle_sym_name );

   CHECKC( ratio >= sym_itr->init_collateral_ratio, err::RATE_EXCEEDED, "callation ratio exceeded" )

   loaners.modify(itr, _self, [&](auto& row){
      row.avl_collateral_quant = remain_collateral_quant;
   });

   syms.modify(sym_itr, _self, [&](auto& row){
      row.avl_collateral_quant   -= quant;
   });

   TRANSFER( sym_itr->sym.get_contract(), from, quant,TYPE_REDEEM + ":" + sym_itr->sym.get_symbol().code().to_string());
}

//计算抵押率
uint64_t tyche_loan::get_callation_ratio(const asset& collateral_quant, const asset& principal, const name& oracle_sym_name ){
   if(principal.amount == 0)
      return PCT_BOOST;
   auto price           = get_index_price( oracle_sym_name );
   auto current_price   = asset( price, principal.symbol );
   auto total_quant     = calc_quote_quant(collateral_quant, current_price);
   auto ratio           = total_quant.amount * PCT_BOOST / principal.amount;

   return ratio;
}

//根据用户支付的金额, 算出抵押物的数量
asset tyche_loan::calc_collateral_quant( const asset& collateral_quant, const asset& paid_principal_quant, const name& oracle_sym_name){
   CHECKC( collateral_quant.amount > 0, err::INCORRECT_AMOUNT, "collateral_quant must positive" )
   CHECKC( paid_principal_quant.amount > 0, err::INCORRECT_AMOUNT, "principal_quant must positive" )

   auto price                 = get_index_price( oracle_sym_name );
   auto settle_price          = calc_quant( asset( price, paid_principal_quant.symbol ), _gstate.liquidation_price_ratio );
   auto paid_collateral_quant = calc_base_quant( paid_principal_quant, settle_price, collateral_quant.symbol );
   CHECKC(collateral_quant >= paid_collateral_quant, err::INCORRECT_AMOUNT, "paid_principal_quant must less than paid_collateral_quant" )
   return paid_collateral_quant;
}

uint64_t tyche_loan::get_index_price( const name& base_code ){
    const auto& prices = _price_conf().prices;
    auto itr =  prices.find(base_code);
    CHECKC(itr != prices.end(), err::RECORD_NOT_FOUND, "price not found: " + base_code.to_string()  )
    return itr->second;
}

const price_global_t& tyche_loan::_price_conf() {
    if(!_global_prices_ptr) {
        CHECKC(_gstate.price_oracle_contract.value != 0,err::SYSTEM_ERROR, "Invalid price_oracle_contract");
        _global_prices_tbl_ptr =  std::make_unique<price_global_t::idx_t>(_gstate.price_oracle_contract, _gstate.price_oracle_contract.value);
        _global_prices_ptr = std::make_unique<price_global_t>(_global_prices_tbl_ptr->get());
    }
   TRACE_L("_price_conf end");
   return *_global_prices_ptr;
}

/***
 * @brief 1. 用户打入MUSDT，获得抵押物
 *           更具当前价格, 如果抵押率< 150%, 则按当前eth 价格87%的价格售卖,平台得到10%的利润,用户得到3%价格差奖励
*/
void tyche_loan::_liquidate( const name& from, const name& liquidater, const symbol& callat_sym, const asset& quant ){
   CHECKC(quant.amount >= 0, err::INCORRECT_AMOUNT, "amount must positive")
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto itr = syms.find(callat_sym.code().raw());
   CHECKC(itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");

   auto loaner = loaner_t::tbl_t(_self, _get_lower(callat_sym).value);
   auto loaner_itr = loaner.find(liquidater.value);
   CHECKC(loaner_itr != loaner.end(), err::RECORD_NOT_FOUND, "account not existed");

   asset total_interest = _get_dynamic_interest(loaner_itr->avl_principal, loaner_itr->term_settled_at, eosio::current_time_point());
   asset need_pay_interest = loaner_itr->unpaid_interest + total_interest;
   asset need_settle_quant = loaner_itr->avl_principal + need_pay_interest;
   auto ratio = get_callation_ratio(loaner_itr->avl_collateral_quant, need_settle_quant, itr->oracle_sym_name);
   CHECKC( ratio <= itr->liquidation_ratio, err::RATE_EXCEEDED, "callation ratio exceeded" )
   

   auto price           = get_index_price( itr->oracle_sym_name );
   auto current_price   = asset( price, quant.symbol );

   if(ratio <= itr->liquidation_ratio && ratio >= itr->force_liquidate_ratio) {
      CHECKC( need_pay_interest <= quant, err::OVERSIZED, "must pay more than interest");

      auto paid_amount = divide_decimal(need_settle_quant.amount, _gstate.liquidation_penalty_ratio, PCT_BOOST );
      auto paid_quant = asset(paid_amount, need_settle_quant.symbol);

      if( paid_quant <= quant) {
         auto return_quant = quant - paid_quant;
         TRANSFER( _gstate.loan_token.get_contract(), from, return_quant, TYPE_RUTURN_BACK + ":" + itr->sym.get_symbol().code().to_string() );

      }

      auto return_collateral_quant = calc_collateral_quant(loaner_itr->avl_collateral_quant, paid_quant, itr->oracle_sym_name);
      //把抵押物转给协议平仓的人
      TRANSFER( itr->sym.get_contract(), from, return_collateral_quant, TYPE_RUTURN_BACK + ":" + itr->sym.get_symbol().code().to_string());
      //平台内结算
      //添加平台金额
      auto platform_quant = paid_quant - need_settle_quant;
      auto paid_principal = need_settle_quant - need_pay_interest;
      CHECKC( paid_principal.amount > 0, err::INCORRECT_AMOUNT, "paid_principal must positive" )
      CHECKC( paid_principal <= loaner_itr->avl_principal, err::INCORRECT_AMOUNT, "principal need < avl_principal" )
      _add_fee(platform_quant);
      // CHECKC(false, err::RATE_EXCEEDED, "test callation ratio: "  + to_string(ratio) + "return_collateral_quant: " + return_collateral_quant.to_string() + " paid_principal:" + paid_principal.to_string());
      //结算用户质押物
      NOTIFY_TRANSFER_ACTION(liquidater, _self, paid_principal, TYPE_LIQUDATE +":" + itr->sym.get_symbol().code().to_string());
      //通知转账消息用户MUSDT -> 平台
      NOTIFY_TRANSFER_ACTION(liquidater, _self, need_settle_quant, TYPE_LIQUDATE + ":"+ itr->sym.get_symbol().code().to_string() + ":" + TYPE_SEND_BACK); 
      loaner.modify(loaner_itr, _self, [&](auto& row){
         row.avl_collateral_quant   -= return_collateral_quant;              //减少抵押物
         row.avl_principal          -= paid_principal; 
         row.unpaid_interest        = asset(0, _gstate.loan_token.get_symbol());
         row.paid_interest          += need_pay_interest;
         row.term_settled_at        = eosio::current_time_point();
      });

      liqlog_t liqlog = {_global_state->new_liqlog_id(), "liq"_n, liquidater, from,
                  need_settle_quant, return_collateral_quant,  paid_principal, current_price,
                  ratio, eosio::current_time_point()};
      NOTIFY_LIQ_ACTION(liqlog);
      return;
   } else {
      if( quant.amount > 0 ){
         TRANSFER( _gstate.loan_token.get_contract(), from, quant, TYPE_RUTURN_BACK + ":"+ itr->sym.get_symbol().code().to_string() );
      }
      syms.modify(itr, _self, [&](auto& row){
         row.total_force_collateral_quant  += loaner_itr->avl_collateral_quant;
         row.total_force_principal         += loaner_itr->avl_principal;
         row.avl_force_collateral_quant   += loaner_itr->avl_collateral_quant;
         row.avl_force_principal          += loaner_itr->avl_principal;
      });

      liqlog_t liqlog = {_global_state->new_liqlog_id(), "forceliq"_n, liquidater, from,
         need_settle_quant, loaner_itr->avl_collateral_quant,loaner_itr->avl_principal, current_price,
         ratio, eosio::current_time_point()};
         //通知转账消息用户METH -> 平台
      NOTIFY_TRANSFER_ACTION(liquidater, _self, loaner_itr->avl_collateral_quant, TYPE_FORCECLOSE + ":" +itr->sym.get_symbol().code().to_string());
      //通知转账消息用户MUSDT -> 平台
      NOTIFY_TRANSFER_ACTION(liquidater, _self, loaner_itr->avl_principal, TYPE_FORCECLOSE+ ":" + itr->sym.get_symbol().code().to_string()+ ":" + TYPE_SEND_BACK); 
      //直接没收抵押物
      loaner.modify(loaner_itr, _self, [&](auto& row){
         row.avl_collateral_quant   = asset(0, itr->sym.get_symbol());              //减少抵押物
         row.avl_principal          = asset(0, _gstate.loan_token.get_symbol()); 
         row.unpaid_interest        = asset(0, _gstate.loan_token.get_symbol());
         row.paid_interest          += need_pay_interest;
         row.term_settled_at        = eosio::current_time_point();
      });
      NOTIFY_LIQ_ACTION(liqlog);
   }
}

void tyche_loan::setcallatsym(const extended_symbol& sym, const name& oracle_sym_name) {
   require_auth(_gstate.admin);
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto itr = syms.find(sym.get_symbol().code().raw());
   if( itr == syms.end() ) {
      syms.emplace(_self, [&](auto& row){
         row.sym                          = sym;
         row.oracle_sym_name              = oracle_sym_name;
         row.on_shelf                     = true;
         row.max_principal                =  asset(100000000000 , _gstate.loan_token.get_symbol());
         row.total_force_collateral_quant  = asset(0, sym.get_symbol());
         row.total_force_principal         = asset(0, _gstate.loan_token.get_symbol());
         row.avl_force_collateral_quant   = asset(0, sym.get_symbol());
         row.avl_force_principal          = asset(0, _gstate.loan_token.get_symbol());
         row.total_collateral_quant       = asset(0, sym.get_symbol());
         row.avl_collateral_quant         = asset(0, sym.get_symbol());
         row.total_principal              = asset(0, _gstate.loan_token.get_symbol());
         row.avl_principal                = asset(0, _gstate.loan_token.get_symbol());
      });
   } else {
      syms.modify(itr, _self, [&](auto& row){
         row.sym = sym;
         row.on_shelf = true;
      });
   }
}

void tyche_loan::addinteret(const uint64_t& interest_ratio) {
   require_auth(_gstate.admin);
   CHECKC(interest_ratio > 0, err::INCORRECT_AMOUNT, "interest_ratio must positive")

   auto interests = interest_t::tbl_t(_self, _self.value);
   auto first_itr =  interests.begin();
   if( first_itr != interests.end() ) {
      interests.modify(first_itr, _self, [&](auto& row){
         row.ended_at = eosio::current_time_point();
      });
   } 
   interests.emplace(_self, [&](auto& row){
      row.interest_ratio   = interest_ratio;
      row.begin_at         = eosio::current_time_point();
      row.ended_at         = eosio::current_time_point() + eosio::days(365*10);
   });

}

uint64_t tyche_loan::_get_current_interest_ratio() {
   require_auth(_gstate.admin);

   auto interests = interest_t::tbl_t(_self, _self.value);
   auto first_itr =  interests.begin();
   CHECKC( first_itr != interests.end(), err::RECORD_NOT_FOUND, "not validate interest"); 
   return first_itr->interest_ratio;
}


void tyche_loan::tgetinterest(const asset& principal,  const time_point_sec& started_at, const time_point_sec& ended_at) {
   auto interest = _get_dynamic_interest(principal, started_at, ended_at);
   CHECKC(false, err::SYSTEM_ERROR, "interest: " + interest.to_string() );

}

asset tyche_loan::_get_dynamic_interest( const asset& quant, 
                                          const time_point_sec& time_start, const time_point_sec& time_end){
   auto interests = interest_t::tbl_t(_self, _self.value);
   auto beg_itr   = interests.begin();
   auto begin_at  = time_start;
   auto ended_at  = time_end;
   auto interest = asset(0, quant.symbol);

   while( beg_itr != interests.end() && beg_itr->ended_at > time_start ) {
      interest += _get_interest(quant, beg_itr->interest_ratio, beg_itr->begin_at, ended_at);
      ended_at = beg_itr->begin_at;
      beg_itr++;
   }
   return interest;
}

asset tyche_loan::_get_interest(const asset& quant, const uint64_t& interest_ratio, 
                                 const time_point_sec& started_at, const time_point_sec& ended_at) {
      auto elapsed =  (ended_at - started_at);
      auto elapsed_seconds = elapsed.count() / 1000000;
      CHECKC( elapsed_seconds > 0,              err::TIME_PREMATURE,       "time premature" )

      auto total_unpaid_interest = quant.amount * interest_ratio / YEAR_SECONDS * elapsed_seconds / PCT_BOOST;
      return asset( total_unpaid_interest, quant.symbol );
}


void tyche_loan::forceliq( const name& from, const name& liquidater, const symbol& callat_sym ){
   require_auth(from);
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto itr = syms.find(callat_sym.code().raw());
   CHECKC(itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");

   auto loaner = loaner_t::tbl_t(_self, _get_lower(callat_sym).value);
   auto loaner_itr = loaner.find(liquidater.value);
   CHECKC(loaner_itr != loaner.end(), err::RECORD_NOT_FOUND, "account not existed");

   asset total_interest = _get_dynamic_interest(loaner_itr->avl_principal, loaner_itr->term_settled_at,eosio::current_time_point() );
   asset need_pay_interest = loaner_itr->unpaid_interest + total_interest;
   asset need_settle_quant = loaner_itr->avl_principal + need_pay_interest;
   auto ratio = get_callation_ratio(loaner_itr->avl_collateral_quant, need_settle_quant, itr->oracle_sym_name);
   CHECKC( ratio <= itr->force_liquidate_ratio, err::RATE_EXCEEDED, "callation ratio exceeded" )
   syms.modify(itr, _self, [&](auto& row){
      row.total_force_collateral_quant += loaner_itr->avl_collateral_quant;
      row.total_force_principal        += loaner_itr->avl_principal;
      row.avl_force_collateral_quant += loaner_itr->avl_collateral_quant;
      row.avl_force_principal        += loaner_itr->avl_principal;
   });
   auto price           = get_index_price( itr->oracle_sym_name );
   auto current_price   = asset( price, itr->avl_principal.symbol );

   liqlog_t liqlog = {_global_state->new_liqlog_id(), "forceliq"_n, liquidater, from,
               need_settle_quant, loaner_itr->avl_collateral_quant,  loaner_itr->avl_principal, current_price,
               ratio, eosio::current_time_point()};

   NOTIFY_LIQ_ACTION(liqlog);
   //通知转账消息用户METH -> 平台
   NOTIFY_TRANSFER_ACTION(liquidater, _self, loaner_itr->avl_collateral_quant, TYPE_FORCECLOSE + ":" + itr->sym.get_symbol().code().to_string());
   //通知转账消息用户MUSDT -> 平台
   NOTIFY_TRANSFER_ACTION(liquidater, _self, loaner_itr->avl_principal, TYPE_FORCECLOSE + ":" + itr->sym.get_symbol().code().to_string() + ":" + TYPE_SEND_BACK); 
   //直接没收抵押物
   loaner.modify(loaner_itr, _self, [&](auto& row){
      row.avl_collateral_quant   = asset(0, itr->sym.get_symbol());              //减少抵押物
      row.avl_principal          = asset(0, _gstate.loan_token.get_symbol()); 
      row.unpaid_interest        = asset(0, _gstate.loan_token.get_symbol());
      row.paid_interest          += need_pay_interest;
      row.term_settled_at        = eosio::current_time_point();
   });
  
}

void tyche_loan::_add_fee(const asset& quantity) {
    auto fees           = make_fee_table( get_self() );
    auto it             = fees.find( quantity.symbol.code().raw() );
    if (it != fees.end()) {
        fees.modify(*it, _self, [&](auto &row) {
            row.fees += quantity;
        });
    } else {
        fees.emplace(_self, [&]( auto& row ) {
            row.fees = quantity;
        });
    }
}

asset tyche_loan::_sub_fee(const symbol& sym) {
   auto fee_tbl = make_fee_table( get_self() );
   auto itr = fee_tbl.find( sym.code().raw() );

   CHECKC( itr != fee_tbl.end(), err::RECORD_NOT_FOUND, "The user does not exist or has fee" )
   CHECKC( itr->fees.amount > 0, err::PARAM_ERROR, "not enought balance" )
   auto quant = itr->fees;
   fee_tbl.modify( *itr, _self, [&](auto &row ) {
      row.fees = asset( 0, sym );
   });
   return quant;
}
void tyche_loan::notifyliq( const liqlog_t& liqlog ){
   require_auth(get_self());
   require_recipient(get_self());
}

void tyche_loan::notifytran(const name& from, const name& to, const asset& quants, const string& memo){
   require_auth(get_self());
   require_recipient(get_self());
}


} //namespace tychefi