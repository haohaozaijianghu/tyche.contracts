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
static constexpr name      NUSDT_BANK       = "amax.mtoken"_n;
static constexpr symbol    NUSDT            = symbol(symbol_code("NUSDT"), 6);
#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size())

#define TBL struct [[eosio::table, eosio::contract("usdt.interest")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("usdt.interest")]]

NTBL("global") global_t {
    name                refuel_account  = "usdtrefuel"_n;
    name                usdt_save_contract = "usdt.save"_n;
    bool                enabled;


    EOSLIB_SERIALIZE( global_t, (refuel_account)(usdt_save_contract)(enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;


} //namespace amax