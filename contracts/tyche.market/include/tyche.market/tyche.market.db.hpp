#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/time.hpp>

namespace tychefi {

using namespace eosio;

static constexpr uint64_t RATE_SCALE  = 10'000;          // basis points
static constexpr uint64_t PRICE_SCALE = 10'000;          // price precision
static constexpr uint32_t SECONDS_PER_YEAR = 31'536'000; // 365 days

#define TBL struct [[eosio::table, eosio::contract("tyche.market")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.market")]]

NTBL("global") global_t {
   name        admin;
   bool        paused          = false;
   uint32_t    price_ttl_sec   = 300;
   uint64_t    close_factor_bp = 5000;

   EOSLIB_SERIALIZE( global_t, (admin)(paused)(price_ttl_sec)(close_factor_bp))
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

NTBL("prices") price_feed {
   symbol_code sym_code;
   uint64_t    price;
   time_point  updated_at;

   uint64_t primary_key()const { return sym_code.raw(); }
   EOSLIB_SERIALIZE( price_feed, (sym_code)(price)(updated_at))
};
using prices_t = eosio::multi_index<"prices"_n, price_feed>;

NTBL("reserves") reserve_state {
   symbol_code sym_code;
   name        token_contract;

   // risk params
   uint64_t max_ltv;
   uint64_t liquidation_threshold;
   uint64_t liquidation_bonus;
   uint64_t reserve_factor;

   // interest model
   uint64_t u_opt;
   uint64_t r0;
   uint64_t r_opt;
   uint64_t r_max;

   // accounting
   asset     total_liquidity;
   asset     total_debt;
   asset     total_supply_shares;
   asset     total_borrow_shares;
   asset     protocol_reserve;

   time_point last_updated;
   bool       paused = false;

   uint64_t primary_key()const { return sym_code.raw(); }
   EOSLIB_SERIALIZE( reserve_state, (sym_code)(token_contract)(max_ltv) (liquidation_threshold)
                                 (liquidation_bonus)(reserve_factor)(u_opt)
                                 (r0)(r_opt)(r_max)(total_liquidity)
                                 (total_debt)(total_supply_shares)(total_borrow_shares)
                                 (protocol_reserve)(last_updated)(paused) )
};

using reserves_t = eosio::multi_index<"reserves"_n, reserve_state>;

NTBL("positions") position_row {
   uint64_t    id;
   name        owner;
   symbol_code sym_code;

   asset supply_shares;
   asset borrow_shares;
   bool  collateral = true;

   uint64_t  primary_key()const { return id; }
   uint64_t  by_owner()const { return owner.value; }
   uint128_t by_owner_reserve()const {
      return (uint128_t(owner.value) << 64) | sym_code.raw();
   }
    EOSLIB_SERIALIZE( position_row, (id)(owner)(sym_code)(supply_shares)(borrow_shares)(collateral) )
};

using positions_t = eosio::multi_index<
   "positions"_n, position_row,
   indexed_by<"byowner"_n,
      const_mem_fun<position_row, uint64_t, &position_row::by_owner>>,
   indexed_by<"ownerreserve"_n,
      const_mem_fun<position_row, uint128_t, &position_row::by_owner_reserve>>
>;

} // namespace tychefi