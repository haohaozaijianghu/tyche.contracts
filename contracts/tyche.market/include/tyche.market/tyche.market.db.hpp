#pragma once

#include <eosio/asset.hpp>
#include <eosio/privileged.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include <utils.hpp>

#include <optional>
#include <string>
#include <map>
#include <set>
#include <type_traits>
#include <price.oracle/price.oracle.states.hpp>

namespace tychefi {

using namespace std;
using namespace eosio;

static constexpr uint16_t  PCT_BOOST         = 10000;
static constexpr uint64_t  DAY_SECONDS       = 24 * 60 * 60;
static constexpr uint64_t  YEAR_SECONDS      = 24 * 60 * 60 * 365;
static constexpr uint64_t  YEAR_DAYS         = 365;
static constexpr int128_t  HIGH_PRECISION    = 1'000'000'000'000'000'000; // 1e18

#define TBL struct [[eosio::table, eosio::contract("tyche.market")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.market")]]

NTBL("global") global_t {
    name                admin                   = "tyche.admin"_n;
    name                price_oracle_contract   = "price.oracle"_n;
    bool                enabled                 = true;

    uint64_t            close_factor_bps        = 5000; // 50%

    EOSLIB_SERIALIZE( global_t, (admin)(price_oracle_contract)(enabled)(close_factor_bps) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

TBL reserve_t {
    extended_symbol     asset;
    name                oracle_sym_name; // price oracle symbol

    uint64_t            max_ltv_bps = 0;                // 0-10000
    uint64_t            liq_threshold_bps = 0;          // 0-10000
    uint64_t            liq_bonus_bps = 0;              // e.g. 10500
    uint64_t            reserve_factor_bps = 0;         // 0-10000

    uint64_t            u_opt_bps = 0;                  // 0-10000
    uint64_t            r0_ray = 0;                     // 1e18
    uint64_t            r_opt_ray = 0;                  // 1e18
    uint64_t            r_max_ray = 0;                  // 1e18

    bool                paused = false;
    bool                on_shelf = true;

    asset               total_supply;
    asset               total_borrow;

    int128_t            liquidity_index = HIGH_PRECISION; // 1e18
    int128_t            borrow_index = HIGH_PRECISION;    // 1e18
    time_point_sec      last_update;

    reserve_t() {}

    uint64_t primary_key() const { return asset.get_symbol().code().raw(); }

    typedef eosio::multi_index< "reserves"_n, reserve_t > idx_t;

    EOSLIB_SERIALIZE( reserve_t, (asset)(oracle_sym_name)
                                 (max_ltv_bps)(liq_threshold_bps)(liq_bonus_bps)(reserve_factor_bps)
                                 (u_opt_bps)(r0_ray)(r_opt_ray)(r_max_ray)
                                 (paused)(on_shelf)
                                 (total_supply)(total_borrow)
                                 (liquidity_index)(borrow_index)(last_update) )
};

TBL user_pos_t {
    name        owner;
    asset       supply_shares;
    asset       borrow_shares;
    bool        collateral_enabled = true;

    user_pos_t() {}
    user_pos_t(const name& o): owner(o) {}

    uint64_t primary_key() const { return owner.value; }

    typedef eosio::multi_index< "userpos"_n, user_pos_t > tbl_t;

    EOSLIB_SERIALIZE( user_pos_t, (owner)(supply_shares)(borrow_shares)(collateral_enabled) )
};

TBL globalidx {
    uint64_t        liqlog_id = 0;
};
typedef eosio::singleton< "globalidx"_n, globalidx > global_table;

struct global_state: public globalidx {
    public:
        bool changed = false;

        using ptr_t = std::unique_ptr<global_state>;

        static ptr_t make_global(const name &contract) {
            auto ret = std::make_unique<global_state>();
            ret->_global_tbl = std::make_unique<global_table>(contract, contract.value);

            if (ret->_global_tbl->exists()) {
                static_cast<globalidx&>(*ret) = ret->_global_tbl->get();
            }
            return ret;
        }

        inline uint64_t new_auto_inc_id(uint64_t &id) {
            if (id == 0 || id == std::numeric_limits<uint64_t>::max()) {
                id = 1;
            } else {
                id++;
            }
            change();
            return id;
        }

        inline uint64_t new_liqlog_id() {
            return new_auto_inc_id(liqlog_id);
        }
        inline void change() { changed = true; }

        inline void save(const name &payer) {
            if (changed) {
                auto &g = static_cast<globalidx&>(*this);
                _global_tbl->set(g, payer);
                changed = false;
            }
        }
    private:
        std::unique_ptr<global_table> _global_tbl;
};

struct liqlog_t {
    uint64_t    id;
    name        user;
    name        liquidator;
    extended_symbol debt_asset;
    extended_symbol collateral_asset;
    asset       repaid;
    asset       seized;
    uint64_t    health_factor_bps;
    time_point  deal_time;

    uint64_t primary_key() const { return id; }
};

} // namespace tychefi
