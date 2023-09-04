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
      void rewardrefuel( const name& token_bank, const asset& total_rewards);
   public:
      using onrewardrefuel_action = eosio::action_wrapper<"rewardrefuel"_n, &usdt_save::rewardrefuel>;
};

} //namespace amax
