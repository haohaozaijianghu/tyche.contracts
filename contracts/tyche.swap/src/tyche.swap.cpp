#include <tyche.swap/tyche.swap.hpp>
#include <amax.token.hpp>

#include "safemath.hpp"
#include <utils.hpp>
// #include <eosiolib/time.hpp> 
#include <eosio/time.hpp>

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
inline static asset calc_sharer_rewards(const asset& earner_shares, const int128_t& reward_per_share_delta, const symbol& rewards_symbol) {
   ASSERT( earner_shares.amount >= 0 && reward_per_share_delta >= 0 );
   int128_t rewards = earner_shares.amount * reward_per_share_delta / HIGH_PRECISION;
   // rewards = rewards * get_precision(rewards_symbol)/get_precision(earner_shares.symbol);
   CHECK( rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow" );
   return asset( (int64_t)rewards, rewards_symbol );
}

void tyche_swap::init(const name& admin) {
   require_auth( _self );
   _gstate.admin                    = admin;
   _gstate.enabled                  = true;
}

/**
 * @brief send nasset tokens into nftone marketplace
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo: two formats:
 *       1) redeem:$code - upon transferring in TRUSD to withdraw
 */
void tyche_swap::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   CHECKC(_gstate.enabled, err::PAUSED, "not effective yet");
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );

   if(from == _self || to != _self) return;
   if(quant.symbol == MUSDT) {
      vector<string_view> memo_params = split(memo, ":"); 
      CHECKC(memo_params.size() == 1, err::PARAM_ERROR, "memo invalid")
      auto fft_quant = asset_from_string(memo_params[0]);
      CHECKC(fft_quant.symbol == FFT, err::SYMBOL_MISMATCH, "fft_quant must be FFT")
      CHECKC(fft_quant.amount > 0, err::NOT_POSITIVE, "fft_quant must be positive")
      TRANSFER( MUSDT_BANK, from, fft_quant, "fft buy" )
      return;
   } else if(quant.symbol == FFT) {
      _gstate.total_fft_quant += quant;
      return;
   }
   CHECKC(false, err::PARAM_ERROR, "invalid memo format: " + from.to_string() + " to: " + to.to_string() + " quant: " + quant.to_string() + " memo: " + memo);
}

void tyche_swap::splitreward(const std::vector<split_parm_t>& parms) {
   require_auth(_gstate.admin);
   for(auto& parm : parms) {
      CHECKC(parm.quant.amount > 0, err::NOT_POSITIVE, "base_quant must be positive")
      CHECKC(parm.memo.size() < 32, err::PARAM_ERROR, "memo size too long")
      TRANSFER( parm.bank, parm.owner, parm.quant, parm.memo)
   }
}

}