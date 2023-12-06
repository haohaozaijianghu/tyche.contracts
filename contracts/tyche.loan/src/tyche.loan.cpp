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

void tyche_loan::init(const name& admin, const name& reward_contract, const name& lp_refueler, const bool& enabled) {
   require_auth( _self );
   _gstate.admin                    = admin;
   _gstate.reward_contract          = reward_contract;
   _gstate.lp_refueler              = lp_refueler;
   _gstate.enabled                  = enabled;
}

/**
 * @brief send nasset tokens into nftone marketplace
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo: two formats:
 *       1) 
 */
void tyche_loan::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   CHECKC(_gstate.enabled, err::PAUSED, "not effective yet");
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );

   if (from == get_self() || to != get_self()) return;
   auto token_bank = get_first_receiver();

   //MUSDT
   if( quant.symbol == _gstate.loan_token.get_symbol() && token_bank == _gstate.loan_token.get_contract() ) { 
      if( from == _gstate.lp_refueler )
         return;
      // onredeem(from, quant);
      return;
   }
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto itr = syms.find(quant.symbol.code().raw());
   CHECKC(itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");
   CHECKC(token_bank == itr->sym.get_contract(), err::CONTRACT_MISMATCH, "symbol not supported");
   onaddcallat(from, quant);
   return;

}


//赎回抵押物，用户打入MUSDT
//internal call
void tyche_loan::onredeem( const name& from, const symbol& collateral_sym, const asset& quant ){

   loaner_t::tbl_t loaners(_self, collateral_sym.code().raw());
   auto itr = loaners.find(from.value);
   CHECKC(itr != loaners.end(),           err::RECORD_NOT_FOUND,  "account not existed")
   CHECKC(itr->avl_principal.amount > 0,  err::OVERSIZED,         "avl_principal must positive")

   //结算利息
   auto total_unpaid_interest = _get_interest(itr->avl_principal, itr->interest_ratio, itr->term_settled_at);

   total_unpaid_interest      += itr->unpaid_interest;
   auto total_paid_interest   = itr->paid_interest;
   auto avl_principal         = itr->avl_principal;
   if (total_unpaid_interest > quant) {   //支付的钱只够利息
      total_unpaid_interest   = total_unpaid_interest - quant;
      total_paid_interest     += quant;
   } else {
      total_unpaid_interest   = asset(0,total_unpaid_interest.symbol );
      total_paid_interest     += total_unpaid_interest;
      auto principal_repay_quant = quant - total_unpaid_interest;
      if(principal_repay_quant > avl_principal) {
         //打USDT给用户
         auto  return_quant = principal_repay_quant - avl_principal;
         TRANSFER( _gstate.loan_token.get_contract(), from, return_quant, "tyche loan return principal" );
         //TODO
         avl_principal        = asset(0, avl_principal.symbol);
      } else {
         avl_principal         -= principal_repay_quant;
      }
   }

   loaners.modify(itr, _self, [&](auto& row){
      row.avl_principal          = avl_principal;
      row.paid_interest          = total_paid_interest;
      row.unpaid_interest        = total_unpaid_interest;
      row.term_settled_at        = eosio::current_time_point();
      row.term_ended_at          = eosio::current_time_point() + eosio::days(_gstate.term_interval_days);
   });
}


//获得更多的MUSDT
void tyche_loan::getmoreusdt(const name& from, const symbol& callat_sym, const asset& quant){
   require_auth(from);
   CHECKC(quant.amount > 0, err::INCORRECT_AMOUNT, "amount must positive")
   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto itr = syms.find(callat_sym.code().raw());
   CHECKC(itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");

   auto loaner = loaner_t::tbl_t(_self, callat_sym.code().raw());
   auto loaner_itr = loaner.find(from.value);
   CHECKC(loaner_itr != loaner.end(), err::RECORD_NOT_FOUND, "account not existed");

   auto ratio = get_callation_ratio(loaner_itr->avl_collateral_quant, loaner_itr->avl_principal + quant);
   CHECKC( ratio >= itr->init_collateral_ratio, err::RATE_EXCEEDED, "callation ratio exceeded" )
   //打USDT给用户
   TRANSFER( _gstate.loan_token.get_contract(), from, quant, "tyche loan" );

   asset total_interest = _get_interest(loaner_itr->avl_principal, loaner_itr->interest_ratio, loaner_itr->term_settled_at);

   //更新用户的MUSDT
   loaner.modify(loaner_itr, _self, [&](auto& row){
      row.avl_principal    += quant;
      row.unpaid_interest  += total_interest;
      row.term_settled_at  = eosio::current_time_point();
   });
}

//增加质押物
void tyche_loan::onaddcallat( const name& from, const asset& quant ){
   loaner_t::tbl_t loaners(_self, quant.symbol.code().raw());
   auto itr = loaners.find(from.value);
   if( itr == loaners.end() ){
      loaners.emplace(_self, [&](auto& row){
         row.owner = from;
         row.cum_collateral_quant   = quant;
         row.avl_collateral_quant   = quant;
         row.avl_principal          = asset(0, _gstate.loan_token.get_symbol());
         row.interest_ratio         = _gstate.interest_ratio;
         row.created_at             = eosio::current_time_point();
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
}

//赎回质押物，就是减少用户的质押物
void tyche_loan::onsubcallat( const name& from, const asset& quant ) {
   require_auth(from);
   loaner_t::tbl_t loaners(_self, quant.symbol.code().raw());
   auto itr = loaners.find(from.value);
   CHECKC(itr != loaners.end(),           err::RECORD_NOT_FOUND,  "account not existed")
   CHECKC(itr->avl_collateral_quant.amount > 0,  err::OVERSIZED,  "avl_principal must positive")
   CHECKC(itr->avl_collateral_quant.amount >= quant.amount, err::OVERSIZED,  "too many callat sub")

   auto remain_collateral_quant = itr->avl_collateral_quant - quant;
   //算新的抵押率
   auto ratio = get_callation_ratio(remain_collateral_quant, itr->avl_principal);

   auto syms = collateral_symbol_t::idx_t(_self, _self.value);
   auto sym_itr = syms.find(quant.symbol.code().raw());
   CHECKC(sym_itr != syms.end(), err::SYMBOL_MISMATCH, "symbol not supported");
   CHECKC( ratio >= sym_itr->init_collateral_ratio, err::RATE_EXCEEDED, "callation ratio exceeded" )

   loaners.modify(itr, _self, [&](auto& row){
      row.avl_collateral_quant = remain_collateral_quant;
   });

   TRANSFER( sym_itr->sym.get_contract(), from, quant, "tyche loan callat redeem" );
}

uint64_t tyche_loan::get_callation_ratio(const asset& collateral_quant, const asset& principal ){
   if(principal.amount == 0)
      return PCT_BOOST;
   auto price           = get_index_price( _get_lower(collateral_quant.symbol) );
   auto current_price   = asset( price, principal.symbol );
   auto total_quant     = calc_quote_quant(collateral_quant, current_price);
   auto ratio           = total_quant.amount * PCT_BOOST / principal.amount;

   return ratio;
}

uint64_t tyche_loan::get_index_price( const name& base_code ){
    const auto& prices = _price_conf().prices;
    auto itr =  prices.find(base_code);
    CHECKC(itr != prices.end(), err::RECORD_NOT_FOUND, "price not found: " + base_code.to_string()  )
    return (itr->second)/ RATIO_PRECISION;
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

asset tyche_loan::_get_interest( const asset& principal, const uint64_t& interest_ratio, const time_point_sec& term_settled_at ) {
   auto elapsed =  (time_point_sec(current_time_point()) - term_settled_at);
   auto elapsed_seconds = elapsed.count() / 1000000;
   CHECKC( elapsed_seconds > 0,        err::TIME_PREMATURE,       "time premature" )
   auto total_unpaid_interest = principal * interest_ratio / YEAR_SECONDS * elapsed_seconds / PCT_BOOST;
   CHECKC( total_unpaid_interest.amount > 0,  err::INCORRECT_AMOUNT,     "interest must positive" )
   return total_unpaid_interest;

}

}