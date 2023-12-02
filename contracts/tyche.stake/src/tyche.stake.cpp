#include <tyche.stake/tyche.stake.hpp>
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


void tyche_stake::init(const name& admin, const name& lp_refueler, const extended_symbol& principal_token, const extended_symbol& lp_token) {
   require_auth( _self );
   _gstate.admin                    = admin;
   _gstate.lp_refueler              = lp_refueler;
   _gstate.principal_token          = principal_token;
   _gstate.lp_token                 = lp_token;
   _gstate.enabled                  = true;
   _gstate.min_deposit_amount       = asset(10'000000, principal_token.get_symbol());
   _gstate.total_supply             = asset(0, principal_token.get_symbol());
}

/**
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo: two formats:
 *       1) redeem:$code - upon transferring in TRUSD to withdraw
 *       2) deposit:$code  - codes: 1,30,90,180,360
 */
void tyche_stake::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   CHECKC(_gstate.enabled, err::PAUSED, "not effective yet");
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );

   if (from == get_self() || to != get_self()) return;
   auto token_bank = get_first_receiver();

   if (token_bank == _gstate.principal_token.get_contract()) {
      CHECKC(quant.symbol == _gstate.principal_token.get_symbol(), err::SYMBOL_MISMATCH, "symbol mismatch");
      CHECKC(quant.amount >= _gstate.min_deposit_amount.amount, err::NOT_POSITIVE, "deposit amount must be positive");
    
   } else if (token_bank == _gstate.lp_token.get_contract()) {
      CHECKC(quant.symbol == _gstate.lp_token.get_symbol(), err::SYMBOL_MISMATCH, "symbol mismatch");
      CHECKC(quant.amount > 0, err::NOT_POSITIVE, "deposit amount must be positive");
      CHECKC(memo.size() > 0, err::PARAM_ERROR, "memo is empty");

   } else {
      eosio::check(false, "unknown token bank");
   }
}

void tyche_stake::_check_point(const name& earner, lock_balance_st& old_locked, lock_balance_st& new_locked) {
   auto u_old = point_t();
   auto u_new = point_t();
   uint128_t old_dslope = 0;
   uint128_t new_dslope = 0;
   auto _epoch          = _gstate.point_history_epoch;
   auto now             = current_time_point().sec_since_epoch();

   if(earner != name("")) {
      if (old_locked.end >= now && old_locked.quant.amount > 0) {
         u_old.slope    = get_slope(old_locked.quant.amount * MULTIPLIER , MAXTIME);
         u_old.bias     = get_bias(u_old.slope , (old_locked.end - now));
      }

      if (new_locked.end > now && new_locked.quant.amount > 0) {
         u_new.slope    = get_slope(new_locked.quant.amount * MULTIPLIER , MAXTIME);
         u_new.bias     = get_bias(u_new.slope , (int128_t)(new_locked.end - now));
      }

      old_dslope = _gstate.slope_changes[old_locked.end];
      if (new_locked.end != 0) {
         if (new_locked.end == old_locked.end)
            new_dslope = old_dslope;
         else
            new_dslope = _gstate.slope_changes[new_locked.end];
      }
   }

   global_point_history_t last_point = global_point_history_t();
   last_point.epoch = 0;
   last_point.bias = 0;
   last_point.slope = 0;
   last_point.block_time = now;
   global_point_history_t::tbl_t point_history(_self, _self.value);

   if( _epoch > 0 ){
      auto itr = point_history.find(_epoch);
      CHECKC(itr != point_history.end(), err::NOT_STARTED, "no locked balance");
      last_point = *itr;
   }

   auto last_checkpoint       = last_point.block_time;
   auto initial_last_point    = last_point;

   auto t_i = (last_checkpoint / WEEK) * WEEK;
   for( uint64_t i=0; i<255; i++ ){
      t_i += WEEK;
      uint128_t d_slope = 0;
      if( t_i > now ){
         t_i = now;
      } else {
         d_slope = _gstate.slope_changes[t_i];
      }
      last_point.bias += ( d_slope) * (t_i - last_checkpoint) / MULTIPLIER;

      last_point.slope += d_slope;
      if (last_point.bias < 0)  // This can happen
         last_point.bias = 0;
      last_checkpoint         = t_i;
      last_point.block_time   = t_i;

      _epoch += 1;
      if (t_i == now) {
         break;
      } else {
         point_history.emplace( _self, [&]( auto& row ) {
            row.epoch       = _epoch;
            row.bias        = last_point.bias;
            row.slope       = last_point.slope;
            row.block_time  = last_point.block_time;
         });
      }
   }

   _gstate.point_history_epoch = _epoch;

   if (earner != name("")){
        last_point.slope += (u_new.slope - u_old.slope);
        last_point.bias += (u_new.bias - u_old.bias);
        if (last_point.slope < 0)
            last_point.slope = 0;
        if (last_point.bias < 0)
            last_point.bias = 0;
   }

   point_history.emplace( _self, [&]( auto& row ) {
      row.epoch       = _epoch;
      row.bias        = last_point.bias;
      row.slope       = last_point.slope;
      row.block_time  = last_point.block_time;
   });
   if (earner != name("")){
         // Schedule the slope changes (slope is going down)
         // We subtract new_user_slope from [new_locked.end]
         // and add old_user_slope to [old_locked.end]
        if (old_locked.end > now) {
            // old_dslope was <something> - u_old.slope, so we cancel that
            old_dslope += u_old.slope;
            if (new_locked.end == old_locked.end)
               old_dslope -= u_new.slope;  // It was a new deposit, not extension
            _gstate.slope_changes[old_locked.end] = old_dslope;
        }

        if (new_locked.end > now)
            if (new_locked.end > old_locked.end)
                new_dslope -= u_new.slope ; // old slope disappeared at this point
                _gstate.slope_changes[new_locked.end] = new_dslope;

      user_point_history_t::tbl_t user_point_history(_self, earner.value);
      _gstate.user_epoch = _gstate.user_epoch + 1;
      user_point_history.emplace( _self, [&]( auto& row ) {
         row.epoch       = _gstate.user_epoch;
         row.earner      = earner;
         row.bias        = u_new.bias;
         row.slope       = u_new.slope;
         row.block_time  = now;
      });
   }
}

void tyche_stake::createlock(const name& earner, const asset& quant, const uint64_t& _unlock_time){
   auto now             = current_time_point().sec_since_epoch();

   auto unlock_time = (_unlock_time / WEEK) * WEEK ; // Locktime is rounded down to weeks
   CHECKC(unlock_time > 0, err::NOT_POSITIVE, "unlock time must be positive");
   CHECKC(unlock_time <= now + MAXTIME, err::TIME_EXPIRED, "Voting lock can be 4 years max");
   CHECKC(quant.amount > 0, err::NOT_POSITIVE, "deposit amount must be positive");

   _deposit_for(earner, quant, unlock_time,CREATE_LOCK_TYPE);
}


void tyche_stake::inctime(const name& earner, const uint64_t& unlock_time) {
   auto now             = current_time_point().sec_since_epoch();
   auto _unlock_time = (unlock_time / WEEK) * WEEK ; // Locktime is rounded down to weeks
   asset quant= asset(0, _gstate.principal_token.get_symbol());
   _deposit_for(earner, quant, _unlock_time, INCREASE_UNLOCK_TIME);
}

void tyche_stake::incamount(const name& earner, const asset& quant) {
   auto now             = current_time_point().sec_since_epoch();
   _deposit_for(earner, quant, 0, INCREASE_LOCK_AMOUNT);
}

void tyche_stake::_deposit_for(const name& earner, const asset& quant, const uint64_t& unlock_time, const uint64_t& type){
   _gstate.total_supply += quant;

   earn_stake_locked::tbl_t earns_locked(_self, _self.value);
   lock_balance_st old_locked = {asset(0, quant.symbol), 0};
   lock_balance_st _locked = {asset(0, quant.symbol), 0};
   auto itr = earns_locked.find(earner.value);
   if (itr == earns_locked.end()) {
      CHECKC(unlock_time > 0, err::NOT_POSITIVE, "unlock time must be positive");
      CHECKC(type == CREATE_LOCK_TYPE, err::PARAM_ERROR, "type error");
      earns_locked.emplace(_self, [&](auto& row) {
         row.owner       = earner;
         row.amount      = quant;
         row.end         = unlock_time;
      });
      _locked = {quant, unlock_time};
   } else {
      earns_locked.modify(itr, _self, [&](auto& row) {
         old_locked.quant  = row.amount;
         old_locked.end    = row.end;
         row.amount      += quant;
         if(unlock_time > 0) {
            row.end         = unlock_time;
         }
         _locked = {row.amount, row.end};
      });
   }
   _check_point(earner, old_locked, _locked );
}


void tyche_stake::withdraw(const name& earner){
   require_auth( earner );
   earn_stake_locked::tbl_t earns_locked(_self, _self.value);
   auto itr = earns_locked.find(earner.value);
   CHECKC(itr != earns_locked.end(), err::NOT_STARTED, "no locked balance");
   CHECKC(itr->end <= current_time_point().sec_since_epoch(), err::TIME_PREMATURE, "locked balance not matured yet");
   CHECKC(itr->amount.amount > 0, err::NOT_POSITIVE, "locked balance must be positive");
   _gstate.total_supply -= itr->amount;

   lock_balance_st old_locked = {itr->amount, itr->end};
   earns_locked.erase(itr);

   lock_balance_st _locked = {asset(0, itr->amount.symbol), 0};
   _check_point(earner, old_locked, _locked);
}

void tyche_stake::balance(const name & earner) {
   user_point_history_t::tbl_t user_point_history(_self, earner.value);
   auto itr = user_point_history.begin();
   CHECKC(itr != user_point_history.end(), err::NOT_STARTED, "no locked balance");
   auto amount = itr->bias -(itr->slope * (current_time_point().sec_since_epoch() - itr->block_time));


   auto quant = asset(uint32_t(amount / MULTIPLIER), _gstate.principal_token.get_symbol());

   CHECKC( false, err::NOT_STARTED, earner.to_string() + "no locked balance: " + quant.to_string());
}

void tyche_stake::totalsupply2(const uint64_t& ts) {
   global_point_history_t::tbl_t point_history(_self, _self.value);
   auto itr = point_history.rbegin();
   CHECKC(itr != point_history.rend(), err::NOT_STARTED, "no locked balance");
   auto last_point = *itr;
   auto curr_time =  ts;
   auto t_i  = (itr->block_time / WEEK) * WEEK;
   for (uint64_t i = 0; i < 255; i++)
   {
      t_i += WEEK;
      uint128_t d_slope = 0;
      if (t_i > curr_time) {
         t_i = curr_time;
      }
      else {
         d_slope = _gstate.slope_changes[t_i];
      }
      last_point.bias += (d_slope) * (t_i - last_point.block_time);
      if(t_i == curr_time) 
         break;

      last_point.slope += d_slope;
      last_point.block_time = t_i;
   }
   if (last_point.bias < 0)  // This can happen
      last_point.bias = 0;

   auto quant = asset(uint32_t(last_point.bias / MULTIPLIER), _gstate.principal_token.get_symbol());

   CHECKC(false, err::NOT_STARTED,"time: "  + to_string(curr_time) + " total supply: " + quant.to_string());
}


void tyche_stake::totalsupply() {
   auto now = current_time_point().sec_since_epoch();
   totalsupply2(now);
}

}