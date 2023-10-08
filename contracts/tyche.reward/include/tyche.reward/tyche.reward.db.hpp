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

static constexpr name      MUSDT_BANK       = "amax.mtoken"_n;
static constexpr symbol    MUSDT            = symbol(symbol_code("MUSDT"), 6);
static constexpr name      TRUSD_BANK       = "amax.mtoken"_n;
static constexpr symbol    TRUSD            = symbol(symbol_code("TRUSD"), 6);
#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size())

#define TBL struct [[eosio::table, eosio::contract("tyche.reward")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.reward")]]

NTBL("global") global_t {
    name                refueler_account        = "tyche.admin"_n;
    name                tyche_earn_contract      = "tyche.earn"_n;
    asset               total_interest_quant    = asset(0, MUSDT);      //已打入的利息
    asset               allocated_interest_quant= asset(0, MUSDT);      //根据年化已分配的利息
    asset               redeemed_interest_quant = asset(0, MUSDT);      //已领取的利息
    time_point_sec      instert_allocated_started_at;                   //利息周期结束时间,TODO set
    uint64_t            annual_interest_rate    = 300*48;               //TODO 300

    bool                enabled;
 
    EOSLIB_SERIALIZE( global_t, (refueler_account)(tyche_earn_contract)
                                (total_interest_quant)(allocated_interest_quant)(redeemed_interest_quant)
                                (instert_allocated_started_at)(annual_interest_rate)
                                (enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;


//Scope: _self
TBL reward_t {
    asset               total_reward_quant;         //总奖励
    asset               allocated_reward_quant;     //已分配奖励
    asset               redeemed_reward_quant;      //已领取奖励
    name                bank;                       //奖励代币银行
    string              memo;                       //奖励备注      
    time_point_sec      last_reward_at;             //创建时间
    time_point_sec      updated_at;                 //更新时间
    reward_t() {}
    uint64_t primary_key()const { return total_reward_quant.symbol.code().raw(); }

    typedef multi_index<"rewardinfos"_n, reward_t> tbl_t;

    EOSLIB_SERIALIZE( reward_t, (total_reward_quant)(allocated_reward_quant)(redeemed_reward_quant)
                                    (bank)(memo)(last_reward_at)(updated_at) )
};

} //namespace tychefi