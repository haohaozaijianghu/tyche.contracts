#include <tyche.loan/tyche.loan.hpp>
#include <tyche.reward/tyche.reward.hpp>
#include <amax.token.hpp>
#include <aplink.farm/aplink.farm.hpp>

#include "safemath.hpp"
#include <utils.hpp>
// #include <eosiolib/time.hpp> 
#include <eosio/time.hpp>
#include<tyche.reward/tyche.reward.db.hpp>

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
   oncollateral(from, quant);

   return;



}
//赎回
void tyche_loan::onredeem( const name& from, const symbol& collateral_sym, const asset& quant ){

   loaner_t::tbl_t loaners(_self, collateral_sym.code().raw());
   auto itr = loaners.find(from.value);
   CHECKC(itr != loaners.end(),           err::RECORD_NOT_FOUND,  "account not existed")
   CHECKC(itr->avl_principal.amount > 0,  err::OVERSIZED,         "avl_principal must positive")

   //结算利息
   auto elapsed =  (time_point_sec(current_time_point()) - itr->term_settled_at);
   auto elapsed_seconds = elapsed.count() / 1000000;
   CHECKC( elapsed_seconds > 0,        err::TIME_PREMATURE,       "time premature" )
   auto total_unpaid_interest = itr->avl_principal * itr->interest_ratio / YEAR_SECONDS * elapsed_seconds / PCT_BOOST;
   CHECKC( total_unpaid_interest.amount > 0,  err::INCORRECT_AMOUNT,     "interest must positive" )

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
         //退还用户支付的MUSDT
         //TODO
         avl_principal        = asset(0, avl_principal.symbol);
      } else {
         avl_principal         -= principal_repay_quant;
      }
   }

   loaners.modify(itr, _self, [&](auto& row){

   });

}

//增加质押物
void tyche_loan::oncollateral( const name& from, const asset& quant ){
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
         row.term_ended_at          = eosio::current_time_point() + eosio::days(365*2);
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
}