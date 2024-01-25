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
   UNAVAILABLE_PURCHASE = 20,
   RATE_EXCEEDED        = 21,
   PARAMETER_INVALID    = 22,
   SYSTEM_ERROR         = 200

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
   //user
   ACTION onsubcallat( const name& from, const asset& quant );

   ACTION getmoreusdt( const name& from, const symbol& callat_sym, const asset& quant );

   ACTION forceliq( const name& from, const name& liquidator, const symbol& callat_sym );

   //admin
   ACTION init(const name& admin, const name& lp_refueler, 
               const name& price_oracle_contract,
               const name& tyche_proxy_contract,
               const bool& enabled);
               
   ACTION addinterest(const uint64_t& interest_ratio);
   ACTION setliqpratio(const uint64_t& liquidation_price_ratio);
   ACTION setcallatsym( const extended_symbol& sym, const name& oracle_sym_name );
   ACTION setinitratio(const symbol& sym, const uint64_t& ratio);
   ACTION setcollquant(const symbol& sym, const asset& min_collateral_quant, const asset& max_collateral_quant);

   //admin
   ACTION tgetprice( const symbol& collateral_sym );
   ACTION tgetliqrate( const name& owner, const symbol& collateral_sym );
   ACTION tgetinterest(const asset& principal, const time_point_sec& started_at, const time_point_sec& ended_at );

   ACTION sendtoearn( const asset& quant );

   ACTION notifyliq( const liqlog_t& liqlog );
   using notifyliq_action   = action_wrapper<"notifyliq"_n,  &tyche_loan::notifyliq>;
   ACTION notifytran(const name& from, const name& to, const asset& quants, const string& memo);
   using notifytranfer_action   = action_wrapper<"notifytran"_n,  &tyche_loan::notifytran>;

   private:

      //清算
      void _liquidate( const name& from, const name& liquidator, const symbol& callat_sym, const asset& quant );

      asset calc_collateral_quant( const asset& collateral_quant, const asset& paid_principal_quant, const name& oracle_sym_name, asset& settle_price);

      void _on_pay_musdt( const name& from, const symbol& collateral_sym, const asset& quant );

      void _on_add_callateral( const name& from, const name& token_bank, const asset& quant );

      uint64_t get_callation_ratio(const asset& collateral_quant, const asset& principal, const name& oracle_sym_name);

      uint64_t get_index_price( const name& base_code );

      void _add_fee(const asset& quantity);

      asset _sub_fee(const symbol& sym);

      uint64_t _get_current_interest_ratio();
      asset _get_dynamic_interest( const asset& quant, const time_point_sec& time_start, const time_point_sec& time_end);

      const price_global_t& _price_conf();

      asset _get_interest( const asset& principal, const uint64_t& interest_ratio, const time_point_sec& started_at, const time_point_sec& term_settled_at );

      name _get_lower( const symbol& base_symbol) {
         auto str = ( base_symbol.code().to_string() );
         std::transform(str.begin(), str.end(),str.begin(), ::tolower);
         return name(str);
      }

      global_singleton     _global;
      global_t             _gstate;
      dbc                  _db;
      global_state::ptr_t   _global_state;

      std::unique_ptr<price_global_t::idx_t>    _global_prices_tbl_ptr;
      std::unique_ptr<price_global_t>     _global_prices_ptr;

};
} //namespace tychefi
