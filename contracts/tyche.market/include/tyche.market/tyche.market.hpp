#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/action.hpp>

#include <string>

#include <tyche.market/tyche.market.db.hpp>
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

class [[eosio::contract("tyche.market")]] tyche_market : public contract {
   public:
      using contract::contract;

   tyche_market(eosio::name receiver, eosio::name code, datastream<const char*> ds): contract(receiver, code, ds),
        _global(get_self(), get_self().value), _db(_self),
        _global_state(global_state::make_global(get_self()))
    {
      _gstate = _global.exists() ? _global.get() : global_t{};
    }

    ~tyche_market() {
      _global.set( _gstate, get_self() );
      _global_state->save(get_self());
    }

   [[eosio::on_notify("*::transfer")]]
   void ontransfer(const name& from, const name& to, const asset& quants, const string& memo);

   // admin
   ACTION init(const name& admin, const name& price_oracle_contract, const bool& enabled);
   ACTION setreserve(const extended_symbol& sym, const name& oracle_sym_name,
                     const uint64_t& max_ltv_bps, const uint64_t& liq_threshold_bps,
                     const uint64_t& liq_bonus_bps, const uint64_t& reserve_factor_bps,
                     const uint64_t& u_opt_bps, const uint64_t& r0_ray,
                     const uint64_t& r_opt_ray, const uint64_t& r_max_ray,
                     const bool& paused);
   ACTION pauserv(const symbol& sym, const bool& paused);
   ACTION setclosefac(const uint64_t& close_factor_bps);
   ACTION setoracle(const name& price_oracle_contract);

   // user
   ACTION borrow(const name& from, const extended_symbol& borrow_sym, const asset& amount);
   ACTION repay(const name& from, const name& on_behalf_of, const extended_symbol& borrow_sym, const asset& amount);
   ACTION withdraw(const name& from, const extended_symbol& supply_sym, const asset& amount);
   ACTION setcollat(const name& from, const extended_symbol& sym, const bool& enabled);
   ACTION liquidate(const name& liquidator, const name& user,
                    const extended_symbol& debt_sym, const extended_symbol& collateral_sym,
                    const asset& repay_amount);

   ACTION gethf(const name& user);

   private:
      void _accrue_reserve(reserve_t& reserve);
      int128_t _calc_borrow_rate_ray(const reserve_t& reserve) const;
      int128_t _calc_supply_rate_ray(const reserve_t& reserve, const int128_t& borrow_rate) const;

      asset _to_underlying(const asset& shares, const int128_t& index) const;
      asset _to_shares(const asset& amount, const int128_t& index) const;

      uint64_t _health_factor_bps(const name& user) const;
      void _get_account_values(const name& user, int128_t& collateral_value_ray, int128_t& borrow_value_ray) const;

      uint64_t _get_price_ray(const name& oracle_sym_name) const;
      const price_global_t& _price_conf() const;

      reserve_t _get_reserve(const extended_symbol& sym) const;
      void _set_reserve(const reserve_t& reserve);

      global_singleton     _global;
      global_t             _gstate;
      dbc                  _db;
      global_state::ptr_t  _global_state;

      mutable std::unique_ptr<price_global_t::idx_t> _global_prices_tbl_ptr;
      mutable std::unique_ptr<price_global_t> _global_prices_ptr;
};

} // namespace tychefi
