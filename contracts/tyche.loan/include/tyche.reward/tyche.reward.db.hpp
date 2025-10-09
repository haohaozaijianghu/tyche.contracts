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

struct [[eosio::table("global"), eosio::contract("tyche.reward")]]rewardglobal_t {
    name                refueler_account            = "tyche.admin"_n;
    name                tyche_earn_contract         = "tyche.earn11"_n;
    asset               total_interest_quant        = asset(0, MUSDT);      //已打入的利息
    asset               allocated_interest_quant    = asset(0, MUSDT);      //根据年化已分配的利息
    asset               redeemed_interest_quant     = asset(0, MUSDT);      //已领取的利息
    time_point_sec      interest_splitted_at;                               //利息周期结束时间
    uint64_t            annual_interest_rate        = 300;               // 300
    bool                enabled;

    typedef eosio::singleton< "global"_n, rewardglobal_t >  table;


    EOSLIB_SERIALIZE( rewardglobal_t, (refueler_account)(tyche_earn_contract)
                                (total_interest_quant)(allocated_interest_quant)(redeemed_interest_quant)
                                (interest_splitted_at)(annual_interest_rate)
                                (enabled) )
};

} //namespace tychefi