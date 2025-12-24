#pragma once
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <string>
#include <vector>
#include <set>

using namespace eosio;
using std::string;
using std::vector;
using std::set;

namespace amax {

//-----------------------------------
// ⚙️ 1️⃣ 全局配置表 (global_singleton)
//-----------------------------------
struct [[eosio::table("global")]] global_t {
    set<name> operators;     // 合约部署者
    name      admin;         // 管理员

    // === 自增ID计数器 ===
    uint64_t next_pool_id   = 1;
    uint64_t next_fund_id   = 1;
    uint64_t next_settle_id = 1;
    uint64_t next_cycle_id  = 1;

    EOSLIB_SERIALIZE(global_t,
        (operators)(admin)
        (next_pool_id)(next_fund_id)(next_settle_id)(next_cycle_id)
    )
};
typedef eosio::singleton<"global"_n, global_t> global_singleton;


//-----------------------------------
// 📅 2️⃣ 周期定义表 (cycle_table)
//-----------------------------------
struct [[eosio::table, eosio::contract("rewardlb")]] cycle_t {
    uint64_t   id;                // 自增主键（来自 global.next_cycle_id）
    name       board_category;    // 榜单业务类型: "invite" / "turnover"
    name       board_type;        // 榜单周期类型: "D" / "W" / "M"
    uint64_t   period_id;         // 周期号（如 20251013）
    asset      total_pool_payout; // 本期总发放金额
    time_point award_at;          // 发放完成时间
    time_point updated_at;        // 更新时间

    uint64_t primary_key() const { return id; }

    // 二级索引：按 (board_category, board_type, period_id)
    uint128_t by_compkey() const {
        return (uint128_t(board_category.value) << 64)
             | (uint64_t(board_type.value) ^ period_id);
    }

    EOSLIB_SERIALIZE(cycle_t,
        (id)(board_category)(board_type)(period_id)
        (total_pool_payout)(award_at)(updated_at)
    )
};
typedef eosio::multi_index<
    "cycles"_n,
    cycle_t,
    indexed_by<"bycompkey"_n, const_mem_fun<cycle_t, uint128_t, &cycle_t::by_compkey>>
> cycle_table;


//-----------------------------------
// 🧮 3️⃣ 分红配置项 (rank_item_t)
//-----------------------------------
struct rank_item_t {
    uint8_t rank_no;       // 名次（1~10）
    double  percentage;    // 分红比例（百分比，如30=30%）
    EOSLIB_SERIALIZE(rank_item_t, (rank_no)(percentage))
};


//-----------------------------------
// 🏦 4️⃣ 奖励池信息表 (pools_table)
//-----------------------------------
struct [[eosio::table, eosio::contract("rewardlb")]] pool_t {
    uint64_t    pool_id;
    name        token_contract;
    symbol      sym;

    asset       total_funded;     // 累计注资总额
    asset       remaining;        // 当前余额
    vector<rank_item_t> rank_config;

    bool        active;
    time_point  updated_at;

    uint64_t primary_key() const { return pool_id; }
    uint64_t by_symcode() const { return sym.code().raw(); }

    EOSLIB_SERIALIZE(pool_t,
        (pool_id)(token_contract)(sym)
        (total_funded)(remaining)
        (rank_config)
        (active)(updated_at)
    )
};
typedef eosio::multi_index<
    "pools"_n, pool_t,
    indexed_by<"bysymcode"_n, const_mem_fun<pool_t, uint64_t, &pool_t::by_symcode>>
> pools_table;


//-----------------------------------
// 💰 5️⃣ 注资记录表 (fund_table)
//-----------------------------------
struct [[eosio::table, eosio::contract("rewardlb")]] fund_t {
    uint64_t    id;
    uint64_t    pool_id;
    name        from;
    name        token_contract;
    asset       quantity;
    string      memo;
    time_point  timestamp;
    uint128_t   ext_fund_id;

    uint64_t primary_key() const { return id; }
    uint64_t by_pool() const { return pool_id; }
    uint128_t by_extid() const { return ext_fund_id; }

    EOSLIB_SERIALIZE(fund_t,
        (id)(pool_id)(from)(token_contract)(quantity)(memo)(timestamp)(ext_fund_id)
    )
};
typedef eosio::multi_index<
    "funds"_n,
    fund_t,
    indexed_by<"bypool"_n,  const_mem_fun<fund_t, uint64_t,  &fund_t::by_pool>>,
    indexed_by<"byextid"_n, const_mem_fun<fund_t, uint128_t, &fund_t::by_extid>>
> fund_table;


//-----------------------------------
// 🏁 6️⃣ 排行榜表 (board_table)
//-----------------------------------
struct [[eosio::table, eosio::contract("rewardlb")]] board_t {
    uint64_t          cycle_id;        // 所属周期 ID
    std::vector<name> top_users;       // 前 N 名用户账号
    uint64_t          pool_id;         // 奖励池 ID
    bool              is_settled = false;
    time_point        timestamp;

    uint64_t primary_key() const { return cycle_id; }
    EOSLIB_SERIALIZE(board_t, (cycle_id)(top_users)(pool_id)(is_settled)(timestamp))
};
typedef eosio::multi_index<"boards"_n, board_t> board_table;


//-----------------------------------
// 🎁 7️⃣ 奖励结算表 (settle_table)
//-----------------------------------
struct [[eosio::table, eosio::contract("rewardlb")]] settle_t {
    uint64_t    id;
    uint64_t    cycle_id;
    uint64_t    rank;
    uint64_t    pool_id;
    name        token_contract;
    name        to;
    asset       quantity;
    string      memo;
    string      ref_id;
    time_point  timestamp;

    uint64_t primary_key() const { return id; }
    uint64_t by_pool() const { return pool_id; }
    uint64_t by_to() const { return to.value; }
    uint64_t by_cycle() const { return cycle_id; }

    EOSLIB_SERIALIZE(settle_t,
        (id)(cycle_id)(rank)(pool_id)(token_contract)
        (to)(quantity)(memo)(ref_id)(timestamp)
    )
};
typedef eosio::multi_index<
    "settles"_n,
    settle_t,
    indexed_by<"bypool"_n,  const_mem_fun<settle_t, uint64_t, &settle_t::by_pool>>,
    indexed_by<"byto"_n,    const_mem_fun<settle_t, uint64_t, &settle_t::by_to>>,
    indexed_by<"bycycle"_n, const_mem_fun<settle_t, uint64_t, &settle_t::by_cycle>>
> settle_table;

} // namespace amax