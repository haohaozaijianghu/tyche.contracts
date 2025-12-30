#pragma once

#include <eosio/action.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <string>

#include "tyche.market.db.hpp"

namespace tychefi {

using namespace eosio;
using std::string;

class [[eosio::contract("tyche.market")]] tyche_market : public contract {
public:
   using contract::contract;

   tyche_market(name receiver, name code, datastream<const char*> ds)
   : contract(receiver, code, ds),
     _global(get_self(), get_self().value) {
      _gstate = _global.exists() ? _global.get() : global_t{};
   }

   ~tyche_market() {
      _global.set(_gstate, get_self());
   }

   ACTION init(name admin);
   ACTION setpause(bool paused);
   ACTION setpricettl(uint32_t ttl_sec);
   ACTION setclosefac(uint64_t close_factor_bp);

   ACTION setprice(symbol_code sym, asset price);

   ACTION setemergency(bool enabled);
   ACTION setemcfg(uint64_t bonus_bp, uint64_t max_bonus_bp, uint64_t backstop_min);

   ACTION setreserve(const symbol_code sym,
                     const uint64_t max_ltv,
                     const uint64_t liq_threshold,
                     const uint64_t liq_bonus,
                     const uint64_t reserve_factor);

   ACTION addreserve(const extended_symbol& asset_sym,
                     const uint64_t& max_ltv,
                     const uint64_t& liq_threshold,
                     const uint64_t& liq_bonus,
                     const uint64_t& reserve_factor,
                     const uint64_t& u_opt,
                     const uint64_t& r0,
                     const uint64_t& r_opt,
                     const uint64_t& r_max);

   ACTION withdraw(name owner, asset quantity);

   ACTION claimint(name owner, symbol_code sym);
   ACTION setcollat(name owner, symbol_code sym, bool enabled);
   ACTION borrow(name owner, asset quantity);

   [[eosio::on_notify("*::transfer")]]
   void on_transfer(const name& from,
                    const name& to,
                    const asset& quantity,
                    const std::string& memo);

private:
   global_singleton _global;
   global_t _gstate;

   // ========= core flows =========
   void _on_supply(const name& owner, const asset& quantity);
   void _on_repay(const name& payer, const name& borrower, const asset& quantity);
   void _on_liquidate(name liquidator,
                      name borrower,
                      symbol_code debt_sym,
                      asset repay_amount,
                      symbol_code collateral_sym);

   // ========= reserve helpers =========
   reserve_state _require_reserve(const symbol& sym);

   // ✅ accrue + 写回（统一时点）
   void _accrue_inplace(reserve_state& res, time_point_sec now);
   void _accrue_and_store(reserves_t& reserves, reserves_t::const_iterator itr) ;

   uint64_t _util_bps(const reserve_state& res) const;
   uint64_t _buffer_bps_by_util(uint64_t util_bps) const;

   int64_t _calc_target_borrow_rate(const reserve_state& res, uint64_t util_bps) const;
   int64_t _calc_borrow_rate(const reserve_state& res, uint64_t util_bps) const;

   // ========= shares conversion =========
   asset _supply_shares_from_amount(const asset& amount,
                                   const asset& total_shares,
                                   const asset& total_amount) const;

   asset _borrow_shares_from_amount(const asset& amount,
                                   const asset& total_shares,
                                   const asset& total_amount) const;

   asset _repay_shares_from_amount(const asset& amount,
                                  const asset& total_shares,
                                  const asset& total_amount) const;

   asset _withdraw_shares_from_amount(const asset& amount,
                                     const asset& total_shares,
                                     const asset& total_amount) const;

   asset _amount_from_shares(const asset& shares,
                            const asset& total_shares,
                            const asset& total_amount) const;

   // ========= valuation =========
   struct valuation {
      int128_t collateral_value     = 0;
      int128_t max_borrowable_value = 0;
      int128_t debt_value           = 0;
   };

   int64_t available_liquidity(const reserve_state& res) const;

   valuation _compute_valuation(name owner);

   void _settle_supply_interest( position_row& pos, const reserve_state& res);

   asset _get_fresh_price(prices_t& prices, symbol_code sym) const;
   void  _check_price_available(symbol_code sym) const;

   // ========= positions =========
   position_row* _get_or_create_position(positions_t& table,
                                        name owner,
                                        symbol_code sym,
                                        const asset& base_symbol_amount);

   void _transfer_out(name token_contract, name to, const asset& quantity, const string& memo);
};

} // namespace tychefi