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
   auto p                           = point_t();
   p.block_time                     = current_time_point().sec_since_epoch();
   // ph.block_num = current_block_num();
   _gstate.point_history[0]         = p;
}

/**
 * @brief send nasset tokens into nftone marketplace
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
}


void tyche_stake::_check_point(const name& earner, lock_balance_st& old_locked, lock_balance_st& new_locked) {
   auto u_old = point_t();
   auto u_new = point_t();
   uint128_t old_dslope = 0;
   uint128_t new_dslope = 0;
   auto _epoch          = _gstate.point_history_epoch;
   auto now             = current_time_point().sec_since_epoch();
   auto curr_block_num       = 0; //get_block_num();

   if(earner == name("")) {
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

   point_t last_point = {0, 0, now, 0};
   if( _epoch > 0){
      last_point = _gstate.point_history[_epoch];
   }

   auto last_checkpoint       = last_point.block_time;
   auto initial_last_point    = last_point;
   uint128_t block_slope = 0;  // dblock/dt
   if (now > last_point.block_time) 
        block_slope = MULTIPLIER * (curr_block_num - last_point.block_num) / (now - last_point.block_time);

   auto t_i = (last_checkpoint / WEEK) * WEEK;

   for( uint64_t i=0; i<255; i++ ){
      t_i += WEEK;
      uint128_t d_slope = 0;
      if( t_i > now ){
         t_i = now;
      } else {
         d_slope = _gstate.slope_changes[t_i];
      }

      last_point.bias += (block_slope + d_slope) * (t_i - last_checkpoint) / MULTIPLIER;
      _epoch += 1;
      if (t_i == now) {
         last_point.block_num = curr_block_num;
         break;
      } else {
         _gstate.point_history[_epoch] = last_point;
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

   _gstate.point_history[_epoch] = last_point;

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

      //   # Now handle user history
      //   user_epoch: uint256 = self.user_point_epoch[addr] + 1

      //   self.user_point_epoch[addr] = user_epoch
      //   u_new.ts = block.timestamp
      //   u_new.blk = block.number
      //   self.user_point_history[addr][user_epoch] = u_new
   }

}




}