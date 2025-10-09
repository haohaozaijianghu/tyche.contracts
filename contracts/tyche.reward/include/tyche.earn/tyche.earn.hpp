#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/action.hpp>

#include <string>

#include <wasm_db.hpp>

namespace tychefi {

using namespace eosio;

class tyche_earn {
   public:
      [[eosio::action]]
      void refuelreward( const name& token_bank, const asset& total_rewards, const uint64_t& days, const uint64_t& );

      [[eosio::action]]
      void refuelintrst( const name& token_bank, const asset& total_rewards, const uint64_t& seconds );

   public:
      using onrefuelreward_action = eosio::action_wrapper<"refuelreward"_n, &tyche_earn::refuelreward>;
      using onrefuelintrst_action = eosio::action_wrapper<"refuelintrst"_n, &tyche_earn::refuelintrst>;
};

} //namespace tychefi