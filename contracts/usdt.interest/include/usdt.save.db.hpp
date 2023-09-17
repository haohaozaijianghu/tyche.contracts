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



#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size()) 

struct earn_pool_reward_t {                             //MBTC,HSTZ,MUSDT
    uint64_t        id;
    asset           total_rewards;                      //总奖励 = unalloted_rewards + unclaimed_rewards + claimed_rewards
    asset           last_rewards;                       //上一次总奖励金额
    asset           unalloted_rewards;                  //未分配的奖励(admin)
    asset           unclaimed_rewards;                  //已分配未领取奖励(customer)
    asset           claimed_rewards;                    //已领取奖励
    int128_t        reward_per_share            = 0;    //每票已分配奖励
    int128_t        last_reward_per_share       = 0;    //奖励发放delta TODO
    time_point_sec  reward_added_at;                    //最近奖励发放时间(admin)
    time_point_sec  prev_reward_added_at;               //前一次奖励发放时间间隔
};
using earn_pool_reward_map = std::map<uint64_t/*reward symbol code*/, earn_pool_reward_t>;

//Scope: _self
TBL earn_pool_t {
    uint64_t                code;                                   //1,2,3,4,5
    asset                   sum_quant               = asset(0, MUSDT);      //历史总存款金额
    asset                   available_quant         = asset(0, MUSDT);      //剩余存款金额
    earn_pool_reward_map    rewards;
    uint64_t                term_interval_sec       = 0;            //多少秒
    uint64_t                share_multiplier        = 1;
    bool                    on_shelf                = true;
    time_point_sec          created_at;

    earn_pool_t() {}
    earn_pool_t(const uint64_t& c): code(c) {}
    uint64_t primary_key()const { return code; }

    typedef multi_index<"earnpools"_n, earn_pool_t> tbl_t;

    EOSLIB_SERIALIZE( earn_pool_t, (code)(sum_quant)(available_quant)(rewards)
                                    (term_interval_sec)(share_multiplier)(on_shelf)(created_at) )
};

} //namespace amax