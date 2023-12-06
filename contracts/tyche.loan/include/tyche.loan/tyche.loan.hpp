#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/action.hpp>

#include <string>

#include <tyche.loan/tyche.loan.db.hpp>
#include <wasm_db.hpp>
namespace tychefi {

using std::string;
using std::vector;

using namespace eosio;
using namespace wasm::db;

enum class err: uint8_t {
   NONE                 = 0,
   RECORD_NOT_FOUND     = 1,
   RECORD_EXISTING      = 2,
   CONTRACT_MISMATCH    = 3,
   SYMBOL_MISMATCH      = 4,
   PARAM_ERROR          = 5,
   MEMO_FORMAT_ERROR    = 6,
   PAUSED               = 7,
   NO_AUTH              = 8,
   NOT_POSITIVE         = 9,
   NOT_STARTED          = 10,
   OVERSIZED            = 11,
   TIME_EXPIRED         = 12,
   TIME_PREMATURE       = 13,
   ACTION_REDUNDANT     = 14,
   ACCOUNT_INVALID      = 15,
   FEE_INSUFFICIENT     = 16,
   PLAN_INEFFECTIVE     = 17,
   STATUS_ERROR         = 18,
   INCORRECT_AMOUNT     = 19,
   UNAVAILABLE_PURCHASE = 20

};

/**
 * The `tyche.loan` sample system contract defines the structures and actions that allow users to create, issue, and manage tokens for AMAX based blockchains. It demonstrates one way to implement a smart contract which allows for creation and management of tokens. It is possible for one to create a similar contract which suits different needs. However, it is recommended that if one only needs a token with the below listed actions, that one uses the `tyche.loan` contract instead of developing their own.
 *
 * The `tyche.loan` contract class also implements two useful public static methods: `get_supply` and `get_balance`. The first allows one to check the total supply of a specified token, created by an account and the second allows one to check the balance of a token for a specified account (the token creator account has to be specified as well).
 *
 * The `tyche.loan` contract manages the set of tokens, accounts and their corresponding balances, by using two internal multi-index structures: the `accounts` and `stats`. The `accounts` multi-index table holds, for each row, instances of `account` object and the `account` object holds information about the balance of one token. The `accounts` table is scoped to an eosio account, and it keeps the rows indexed based on the token's symbol.  This means that when one queries the `accounts` multi-index table for an account name the result is all the tokens that account holds at the moment.
 *
 * Similarly, the `stats` multi-index table, holds instances of `currency_stats` objects for each row, which contains information about current supply, maximum supply, and the creator account for a symbol token. The `stats` table is scoped to the token symbol.  Therefore, when one queries the `stats` table for a token symbol the result is one single entry/row corresponding to the queried symbol token if it was previously created, or nothing, otherwise.
 */

class [[eosio::contract("tyche.loan")]] tyche_loan : public contract {
   public:
      using contract::contract;

   tyche_loan(eosio::name receiver, eosio::name code, datastream<const char*> ds): contract(receiver, code, ds),
        _global(get_self(), get_self().value), _db(_self),
        _global_state(global_state::make_global(get_self()))
    {
      _gstate = _global.exists() ? _global.get() : global_t{};
    }

    ~tyche_loan() { _global.set( _gstate, get_self() );
      _global_state->save(get_self());
   }

   [[eosio::on_notify("*::transfer")]]
   void ontransfer(const name& from, const name& to, const asset& quants, const string& memo);
   
   ACTION init(const name& admin, const name& reward_contract, const name& lp_refueler, const bool& enabled);
   
   private:

      void onredeem( const name& from, const asset& quant );

      void oncollateral( const name& from, const asset& quant );
      
      global_singleton     _global;
      global_t             _gstate;
      dbc                  _db;
      global_state::ptr_t   _global_state;

};
} //namespace tychefi
