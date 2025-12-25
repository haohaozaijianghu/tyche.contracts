#pragma once

#include <eosio/asset.hpp>
#include <eosio/action.hpp>
#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>

#include <string>

namespace tychefi {

using namespace eosio;
using std::string;

static constexpr uint64_t RATE_SCALE = 10'000;           // basis points precision
static constexpr uint64_t PRICE_SCALE = 10'000;          // price precision (e.g. USD with 4 decimals)
static constexpr uint32_t SECONDS_PER_YEAR = 31'536'000; // 365d * 24h * 60m * 60s

struct [[eosio::table("global")]] global_state {
   name        admin;
   bool        paused          = false;
   uint32_t    price_ttl_sec   = 300;   // price freshness requirement
   uint64_t    close_factor_bp = 5000;  // maximum portion of debt (in bps) that can be liquidated
};

struct [[eosio::table("prices")]] price_feed {
   symbol_code sym_code;
   uint64_t    price;       // price in quote units with PRICE_SCALE precision
   time_point  updated_at;

   uint64_t primary_key()const { return sym_code.raw(); }
};

struct [[eosio::table("reserves")]] reserve_state {
   symbol_code     sym_code;
   name            token_contract;
   uint64_t        max_ltv;                // basis points
   uint64_t        liquidation_threshold;  // basis points
   uint64_t        liquidation_bonus;      // basis points
   uint64_t        reserve_factor;         // basis points
   uint64_t        u_opt;                  // basis points utilization
   uint64_t        r0;                     // basis points borrow rate at 0% utilization
   uint64_t        r_opt;                  // basis points borrow rate at optimal utilization
   uint64_t        r_max;                  // basis points borrow rate at 100% utilization
   asset           total_liquidity;        // deposit principal + accrued supply interest
   asset           total_debt;             // borrowed principal + accrued interest
   asset           total_supply_shares;    // aggregate supply shares
   asset           total_borrow_shares;    // aggregate borrow shares
   asset           protocol_reserve;       // interest retained by protocol
   time_point      last_updated;
   bool            paused = false;

   uint64_t primary_key()const { return sym_code.raw(); }
};

struct [[eosio::table("positions")]] position_row {
   uint64_t    id;
   name        owner;
   symbol_code sym_code;
   asset       supply_shares;
   asset       borrow_shares;
   bool        collateral = true;

   uint64_t primary_key()const { return id; }
   uint64_t by_owner()const { return owner.value; }
   uint128_t by_owner_reserve()const { return (uint128_t)owner.value << 64 | sym_code.raw(); }
};

using prices_t    = eosio::multi_index<"prices"_n, price_feed>;
using reserves_t  = eosio::multi_index<"reserves"_n, reserve_state>;
using positions_t = eosio::multi_index<
   "positions"_n, position_row,
   indexed_by<"byowner"_n, const_mem_fun<position_row, uint64_t, &position_row::by_owner>>,
   indexed_by<"ownerreserve"_n, const_mem_fun<position_row, uint128_t, &position_row::by_owner_reserve>>>;

class [[eosio::contract("tyche.market")]] tyche_market : public contract {
public:
   using contract::contract;

   tyche_market(name receiver, name code, datastream<const char*> ds);

   [[eosio::action]] void init(name admin);
   [[eosio::action]] void setpause(bool paused);
   [[eosio::action]] void setpricettl(uint32_t ttl_sec);
   [[eosio::action]] void setclosefac(uint64_t close_factor_bp);
   [[eosio::action]] void setprice(symbol_code sym, uint64_t price);
   [[eosio::action]] void addreserve(const extended_symbol& asset_sym,
                                     uint64_t max_ltv,
                                     uint64_t liq_threshold,
                                     uint64_t liq_bonus,
                                     uint64_t reserve_factor,
                                     uint64_t u_opt,
                                     uint64_t r0,
                                     uint64_t r_opt,
                                     uint64_t r_max);

   [[eosio::action]] void supply(name owner, asset quantity);
   [[eosio::action]] void withdraw(name owner, asset quantity);
   [[eosio::action]] void setcollat(name owner, symbol_code sym, bool enabled);
   [[eosio::action]] void borrow(name owner, asset quantity);
   [[eosio::action]] void repay(name payer, name borrower, asset quantity);
   [[eosio::action]] void liquidate(name liquidator,
                                    name borrower,
                                    symbol_code debt_sym,
                                    asset repay_amount,
                                    symbol_code collateral_sym);

private:
   global_state _gstate;

   global_state _read_state()const;
   void         _write_state(const global_state& state);

   reserve_state _require_reserve(const symbol& sym);
   reserve_state _accrue(reserve_state res);
   int64_t       _calc_borrow_rate(const reserve_state& res, uint64_t util_bps)const;

   asset _shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount)const;
   asset _amount_from_shares(const asset& shares, const asset& total_shares, const asset& total_amount)const;

   struct valuation {
      int128_t collateral_value      = 0; // liquidation threshold weighted
      int128_t max_borrowable_value  = 0; // max LTV weighted
      int128_t debt_value            = 0;
   };

   valuation _compute_valuation(name owner);
   uint64_t  _get_fresh_price(prices_t& prices, symbol_code sym)const;
   void      _check_price_available(symbol_code sym)const;

   position_row* _get_or_create_position(positions_t& table, name owner, symbol_code sym, const asset& base_symbol_amount);
   void          _transfer_from(name token_contract, name from, const asset& quantity, const string& memo);
   void          _transfer_out(name token_contract, name to, const asset& quantity, const string& memo);
};

} // namespace tychefi
