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

#define TBL struct [[eosio::table, eosio::contract("usdt.save")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("usdt.save")]]

NTBL("global") global_t {
    name                admin           = "armoniaadmin"_n;
    extended_symbol     voucher_token;          //代币NUSDT
    extended_symbol     principal_token;        //代币MUSDT,用户存入的本金
    asset               mini_deposit_amount;
    name                usdt_interest_contract = "usdt.intst"_n;
    bool                enabled;

    EOSLIB_SERIALIZE( global_t, (admin)(principal_token)(voucher_token)(mini_deposit_amount)(usdt_interest_contract)(enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

struct reward_conf_t {
    asset           total_rewards;                      //总奖励
    asset           allocating_rewards;                 //待分配的奖励
    asset           allocated_rewards;                  //已分配奖励  = total_rewards - allocating_rewards
    asset           claimed_rewards;                    //已领取奖励
    int128_t        rewards_per_vote    = 0;            //每票已分配奖励
};
using reward_conf_map = std::map<uint64_t, reward_conf_t>;

//Scope: _self
TBL save_conf_t {
    uint64_t        code;                //1,30,180,360
    asset           total_deposit_quant     = asset(0, MUSDT);  //总存款金额
    asset           remain_deposit_quant    = asset(0, MUSDT);  //剩余存款金额
    reward_conf_map reward_confs;
    uint64_t        votes_mutli             = 1;
    bool            on_self                 = true;

    time_point_sec  created_at;
    save_conf_t() {}
    save_conf_t(const uint64_t& c) {
        code = c;
    }

    uint64_t primary_key()const { return code; }

    typedef multi_index<"saveconfs"_n, save_conf_t> tbl_t;

    EOSLIB_SERIALIZE( save_conf_t, (code)(total_deposit_quant)(remain_deposit_quant)(reward_confs)
                                    (on_self)(created_at) )
};


struct reward_info_t {
    int128_t            last_rewards_per_vote       = 0;
    asset               unclaimed_rewards;
    asset               claimed_rewards;
    time_point_sec      last_rewards_settled_at;
};

using voted_reward_map = std::map<uint64_t, reward_info_t>;

//Scope: code
//Note: record will be deleted upon withdrawal/redemption
TBL save_account_t {
    name                account;                    //PK
    asset               total_deposit_quant;        //总存款金额
    asset               deposit_quant;              //当前存款金额
    voted_reward_map    voted_rewards;              //每票已分配奖励
    time_point_sec      created_at;
    time_point_sec      started_at;                 //利息开始时间， 一旦有钱充入进来，周期从0开始

    save_account_t() {}
    save_account_t(const name& a) {
        account = a;
    }

    uint64_t primary_key()const { return account.value; }

    typedef multi_index<"saveaccounts"_n, save_account_t> tbl_t;

    EOSLIB_SERIALIZE( save_account_t,   (account)(total_deposit_quant)(deposit_quant)
                                        (voted_rewards)(created_at)(started_at) )
};

TBL reward_symbol_t {
    extended_symbol sym;                    //MUSDT,8@amax.mtoken
    asset           total_reward_quant;     //总奖励金额
    bool            on_self;
    name            reward_type;            //interest | redpack

    reward_symbol_t() {}

    uint64_t primary_key() const { return sym.get_symbol().code().raw(); }
    typedef eosio::multi_index< "rewardsymbol"_n, reward_symbol_t > idx_t;

    EOSLIB_SERIALIZE( reward_symbol_t, (sym)(total_reward_quant)(on_self)(reward_type) )
};

} //namespace amax