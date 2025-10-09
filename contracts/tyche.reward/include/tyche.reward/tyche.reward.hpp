#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/action.hpp>

#include <string>

#include "tyche.reward.db.hpp"
#include <wasm_db.hpp>
namespace tychefi {

using std::string;
using std::vector;

using namespace eosio;
using namespace wasm::db;

static constexpr uint16_t  PCT_BOOST   = 10000;
static constexpr uint64_t  DAY_SECONDS = 24 * 60 * 60;
static constexpr uint64_t  YEAR_DAYS   = 365;
static constexpr int128_t  HIGH_PRECISION    = 1'000'000'000'000'000'000; // 10^18


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
   UNAVAILABLE_PURCHASE = 20,
   PARAM_INVALID        = 21
};

/**
 * The `tyche.reward` sample system contract defines the structures and actions that allow users to create, issue, and manage tokens for FLON based blockchains. It demonstrates one way to implement a smart contract which allows for creation and management of tokens. It is possible for one to create a similar contract which suits different needs. However, it is recommended that if one only needs a token with the below listed actions, that one uses the `tyche.reward` contract instead of developing their own.
 *
 * The `tyche.reward` contract class also implements two useful public static methods: `get_supply` and `get_balance`. The first allows one to check the total supply of a specified token, created by an account and the second allows one to check the balance of a token for a specified account (the token creator account has to be specified as well).
 *
 * The `tyche.reward` contract manages the set of tokens, accounts and their corresponding balances, by using two internal multi-index structures: the `accounts` and `stats`. The `accounts` multi-index table holds, for each row, instances of `account` object and the `account` object holds information about the balance of one token. The `accounts` table is scoped to an eosio account, and it keeps the rows indexed based on the token's symbol.  This means that when one queries the `accounts` multi-index table for an account name the result is all the tokens that account holds at the moment.
 *
 * Similarly, the `stats` multi-index table, holds instances of `currency_stats` objects for each row, which contains information about current supply, maximum supply, and the creator account for a symbol token. The `stats` table is scoped to the token symbol.  Therefore, when one queries the `stats` table for a token symbol the result is one single entry/row corresponding to the queried symbol token if it was previously created, or nothing, otherwise.
 */
class [[eosio::contract("tyche.reward")]] tyche_reward : public contract {
   public:
      using contract::contract;

   tyche_reward(eosio::name receiver, eosio::name code, datastream<const char*> ds): contract(receiver, code, ds),
        _global(get_self(), get_self().value), _db(_self)
    {
        _gstate = _global.exists() ? _global.get() : global_t{};
    }

    ~tyche_reward() { _global.set( _gstate, get_self() ); }

   //管理员打入利息
   [[eosio::on_notify("*::transfer")]]
   void ontransfer(const name& from, const name& to, const asset& quants, const string& memo);

   //提取空投奖励
   ACTION claimreward( const name& to, const name& bank, const asset& total_rewards, const string& memo );

   //提取利息
   ACTION claimintr( const name& to, const name& bank, const asset& total_interest, const string& memo);

   ACTION init(const name& refueler_account, const name& tyche_earn_contract, const bool& enabled);

   ACTION onpoolstart();

   ACTION splitintr();

   ACTION setrate(uint64_t& rate);
   // ACTION initrwd(const asset& quant);


private:
   void _save_reward_info(const asset& quant, const name& token_bank, const uint64_t& pool_conf_code);
   void _sub_reward(const asset& quant, const name& token_bank);
   global_singleton     _global;
   global_t             _gstate;
   dbc                  _db;
};
} //namespace tychefi
