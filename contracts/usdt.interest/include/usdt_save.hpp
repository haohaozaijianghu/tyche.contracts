#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/action.hpp>

#include <string>

#include <wasm_db.hpp>

namespace amax {

class usdt_save {
   public:
      [[eosio::action]] 
      void rewardrefuel( const name& token_bank, const asset& total_rewards, const uint64_t& days, const uint64_t& );

      [[eosio::action]] 
      void intrrefuel( const name& token_bank, const asset& total_rewards, const uint64_t& seconds );

   public:
      using onrewardrefuel_action = eosio::action_wrapper<"rewardrefuel"_n, &usdt_save::rewardrefuel>;
      using onintrrefuel_action = eosio::action_wrapper<"intrrefuel"_n, &usdt_save::intrrefuel>;
};

} //namespace amax
