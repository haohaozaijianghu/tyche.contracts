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
      _global(get_self(), get_self().value)
    {
        _gstate = _global.exists() ? _global.get() : global_t{};
    }

    ~tyche_market() {
        _global.set(_gstate, get_self());
    }


   // =======================
   // Governance
   // =======================
   [[eosio::action]] void init(name admin);
   [[eosio::action]] void setpause(bool paused);
   [[eosio::action]] void setpricettl(uint32_t ttl_sec);
   [[eosio::action]] void setclosefac(uint64_t close_factor_bp);
   [[eosio::action]] void setprice(symbol_code sym, uint64_t price);

   // =======================
   // Reserve Management
   // =======================
   [[eosio::action]] void addreserve(const extended_symbol& asset_sym,
                                       const   uint64_t& max_ltv,
                                       const   uint64_t& liq_threshold,
                                       const   uint64_t& liq_bonus,
                                       const   uint64_t& reserve_factor,
                                       const   uint64_t& u_opt,
                                       const   uint64_t& r0,
                                       const   uint64_t& r_opt,
                                       const   uint64_t& r_max) ;


   [[eosio::action]] void withdraw(name owner, asset quantity);
   [[eosio::action]] void setcollat(name owner, symbol_code sym, bool enabled);
   [[eosio::action]] void borrow(name owner, asset quantity);
   [[eosio::action]] void liquidate(name liquidator,name borrower,symbol_code debt_sym,asset repay_amount,symbol_code collateral_sym);

   [[eosio::on_notify("*::transfer")]]
    void on_transfer(const name& from,
                     const name& to,
                     const asset& quantity,
                     const std::string& memo);

private:

   global_singleton _global;
   global_t _gstate;

   void _on_supply(const name& owner,const asset& quantity);
   void _on_repay(const name& payer,const name& borrower,const asset& quantity);

   reserve_state _require_reserve(const symbol& sym);
   reserve_state _accrue(reserve_state res);
   reserve_state _accrue_and_store(reserves_t& reserves, reserves_t::iterator itr);

   uint64_t _util_bps(const reserve_state& res) const;
   uint64_t _buffer_bps_by_util(uint64_t util_bps) const;

   // v1的曲线函数保持不变：计算“目标利率”
   int64_t _calc_target_borrow_rate(const reserve_state& res, uint64_t util_bps) const;
   // v2：对目标利率做 step cap，产出“本次生效利率”
   int64_t  _calc_borrow_rate(const reserve_state& res, uint64_t util_bps) const;

   asset _supply_shares_from_amount(const asset& amount,
                                    const asset& total_shares,
                                    const asset& total_amount)const;

   asset _borrow_shares_from_amount(const asset& amount,
                                    const asset& total_shares,
                                    const asset& total_amount)const;

   asset _repay_shares_from_amount(const asset& amount,
                                   const asset& total_shares,
                                   const asset& total_amount)const;

   asset _withdraw_shares_from_amount(const asset& amount,
                                      const asset& total_shares,
                                      const asset& total_amount)const;

   asset _amount_from_shares(const asset& shares,
                             const asset& total_shares,
                             const asset& total_amount)const;

   struct valuation {
      int128_t collateral_value     = 0;
      int128_t max_borrowable_value = 0;
      int128_t debt_value           = 0;
   };

   int64_t available_liquidity(const reserve_state& res) {
      uint64_t util_bps = _util_bps(res);
      uint64_t buffer_bps = _buffer_bps_by_util(util_bps);

      int128_t buffer = (int128_t)res.total_liquidity.amount * buffer_bps / RATE_SCALE;
      int128_t avail  = (int128_t)res.total_liquidity.amount
                     - (int128_t)res.protocol_reserve.amount
                     - buffer;

      if (avail <= 0) return 0;
      return (int64_t)avail;
   }

   valuation _compute_valuation(name owner);

   uint64_t _get_fresh_price(prices_t& prices, symbol_code sym)const;
   void     _check_price_available(symbol_code sym)const;

   // =======================
   // Internals
   // =======================
   position_row* _get_or_create_position(positions_t& table,name owner,symbol_code sym,const asset& base_symbol_amount);

   void _transfer_from(name token_contract, name from, const asset& quantity, const string& memo);

   void _transfer_out(name token_contract, name to, const asset& quantity, const string& memo);
};

} // namespace tychefi