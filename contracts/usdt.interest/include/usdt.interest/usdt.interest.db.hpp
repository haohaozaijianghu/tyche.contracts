#pragma once

#include <eosio/asset.hpp>
#include <eosio/privileged.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include <utils.hpp>

// #include <deque>
#include <optional>
#include <string>
#include <map>
#include <set>
#include <type_traits>

namespace amax {

using namespace std;
using namespace eosio;

static constexpr name      MUSDT_BANK       = "amax.mtoken"_n;
static constexpr symbol    MUSDT            = symbol(symbol_code("MUSDT"), 6);
static constexpr name      TRUSD_BANK       = "amax.mtoken"_n;
static constexpr symbol    TRUSD            = symbol(symbol_code("TRUSD"), 6);
#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size())

#define TBL struct [[eosio::table, eosio::contract("usdt.interest")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("usdt.interest")]]

NTBL("global") global_t {
    name                refueler_account        = "tyche.admin"_n;
    name                usdt_save_contract      = "usdt.save"_n;
    asset               total_interest_quant    = asset(0, MUSDT);
    asset               allocated_interest_quant= asset(0, MUSDT);
    time_point_sec      instert_allocated_started_at;                //利息周期结束时间,TODO set
    uint64_t            annual_interest_rate    = 5000;             //TODO 300

    bool                enabled;
 
    EOSLIB_SERIALIZE( global_t, (refueler_account)(usdt_save_contract)
                                (total_interest_quant)(allocated_interest_quant)
                                (instert_allocated_started_at)(annual_interest_rate)
                                (enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

} //namespace amax