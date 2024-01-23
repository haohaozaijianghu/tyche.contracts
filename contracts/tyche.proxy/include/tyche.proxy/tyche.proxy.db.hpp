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

namespace tychefi {

using namespace std;
using namespace eosio;

static constexpr uint64_t  YEAR_SECONDS      = 24 * 60 * 60 * 365;

#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size()) 

#define TBL struct [[eosio::table, eosio::contract("tyche.proxy")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.proxy")]]

NTBL("global") global_t {
    name        tyche_loan_contract     = "tyche.loan"_n;                          //LP TRUSD系统充入账户
    name        tyche_earn_contract    = "tyche.earn"_n;

    name        token_bank              = "amax.mtoken"_n;
    asset       loan_quant              = asset(0, symbol("MUSDT", 6));
    bool        enabled                 = true; 

    EOSLIB_SERIALIZE( global_t, (tyche_loan_contract)(tyche_earn_contract)
                                (token_bank)(loan_quant)(enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

} //namespace tychefi