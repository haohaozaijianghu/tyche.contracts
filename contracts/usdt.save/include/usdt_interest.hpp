#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/action.hpp>

#include <string>

#include <wasm_db.hpp>

namespace amax {

class usdt_interest {
   public:
      [[eosio::action]] 
      void claimreward( const name& to, const name& bank, const asset& rewards, const string& memo );

      //提取利息
      [[eosio::action]] 
      void claimintr( const name& to, const name& bank, const asset& total_interest, const string& memo);

      using claimreward_action   = eosio::action_wrapper<"claimreward"_n,  &usdt_interest::claimreward>;
      using claimintr_action     = eosio::action_wrapper<"claimintr"_n,    &usdt_interest::claimintr>;
};

} //namespace amax
