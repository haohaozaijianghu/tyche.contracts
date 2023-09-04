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
      void claimreward( const name& to, const name& bank, const asset& rewards );

      using claimreward_action = eosio::action_wrapper<"claimreward"_n, &usdt_interest::claimreward>;
};

} //namespace amax
