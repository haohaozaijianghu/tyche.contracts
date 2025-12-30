#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/time.hpp>

namespace tychefi {

using namespace eosio;

// =====================================================
// 常量
// =====================================================
static constexpr uint64_t RATE_SCALE  = 10'000;          // basis points
static constexpr uint32_t SECONDS_PER_YEAR = 31'536'000; // 365 days
static constexpr uint64_t MAX_PRICE_CHANGE_BP = 2000;   // 单次最大 20%
static constexpr uint128_t HIGH_PRECISION = 1'000'000'000'000'000'000ULL; // 1e18

enum class err: uint8_t {
   NONE                 = 0,
   TIME_INVALID         = 1,
   RECORD_EXISTING      = 2,
   RECORD_NOT_FOUND     = 3,
   SYMBOL_MISMATCH      = 4,
   PARAM_ERROR          = 5,
   PAUSED               = 6,
   NO_AUTH              = 7,
   NOT_POSITIVE         = 8,
   NOT_STARTED          = 9,
   OVERSIZED            = 10,
   TIME_EXPIRED         = 11,
   NOTIFY_UNRELATED     = 12,
   ACTION_REDUNDANT     = 13,
   ACCOUNT_INVALID      = 14,
   CONTENT_LENGTH_INVALID = 15,
   NOT_DISABLED          = 16,

};

#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + to_string((int)code) + string("]] ") + msg); }

#define TBL struct [[eosio::table, eosio::contract("tyche.market")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.market")]]

// =====================================================
// 全局配置
// =====================================================
NTBL("global") global_t {
    name                admin;
    bool                paused = false;                         // 全局暂停（所有行为熔断）
    uint32_t            price_ttl_sec = 300;                    // 价格有效期
    uint64_t            close_factor_bp = 5000;                 // 最大单次清算比例
    bool                emergency_mode = false;                 // 紧急模式（极端行情）
    uint64_t            emergency_bonus_bp = 500;               // 紧急模式下额外清算奖励
    uint64_t            max_emergency_bonus_bp = 2000;          // 紧急奖励上限
    uint64_t            backstop_min_reserve = 0;

    EOSLIB_SERIALIZE(
        global_t,
        (admin)(paused)(price_ttl_sec)(close_factor_bp)
        (emergency_mode)(emergency_bonus_bp)
        (max_emergency_bonus_bp)(backstop_min_reserve)
    )
};
using global_singleton = eosio::singleton<"global"_n, global_t>;

// 价格表
NTBL("prices") price_feed {
    symbol_code         sym_code;                               // 被定价资产
    asset               price;                                  // 当前价格（asset，含精度）
    time_point          updated_at;                             // 最近更新时间

    uint64_t primary_key() const { return sym_code.raw(); }

    EOSLIB_SERIALIZE(price_feed, (sym_code)(price)(updated_at))
};
using prices_t = eosio::multi_index<"prices"_n, price_feed>;

// 借款指数（池子级）
struct borrow_index_st {
    uint64_t            index_id = 0;                           // 每次 accrue +1（调试）
    uint128_t           interest_per_share = 0;                 // 累计：每 1 borrow_share 的利息（×1e18）
    uint64_t            borrow_rate_bp = 0;                     // 当前年化利率
    uint64_t            last_borrow_rate_bp = 0;                // 防抖锚点
    time_point_sec      last_updated;
};

// 存款利息指数（池子级）
struct supply_reward_index_st {
    uint64_t            index_id = 0;                           // 每次指数推进 +1
    uint128_t           reward_per_share = 0;                   // 累计：每 1 supply_share 的利息（×1e18）
    time_point_sec      last_updated;
};

// 用户存款利息（用户级）
struct user_supply_interest_st {
    uint64_t            index_id = 0;                            // 对应 pool.index_id（调试）
    uint128_t           last_reward_per_share = 0;               // 用户上次结算锚点
    asset               pending_interest;                        // 已结算、未领取
    asset               claimed_interest;                        // 已领取（统计）
};

// Reserve（资产池）
NTBL("reserves") reserve_state {
    symbol_code         sym_code;                               // 资产
    name                token_contract;                         // 资产合约

    // 风控参数
    uint64_t            max_ltv;                                // 最大贷款价值比
    uint64_t            liquidation_threshold;                  // 清算触发阈值
    uint64_t            liquidation_bonus;                      // 清算奖励
    uint64_t            reserve_factor;                         // 协议抽成

    // 利率模型
    uint64_t            u_opt;                                  // 最优利用率
    uint64_t            r0;                                     // 最低利率
    uint64_t            r_opt;                                  // 最优点利率
    uint64_t            r_max;                                  // 最高利率
    uint64_t            max_rate_step_bp = 200;                 // 单次利率最大变动

    // 资金状态
    asset               total_liquidity;                        // 池子真实可用现金
    asset               total_debt;                             // 池子总借款额
    asset               total_supply_shares;                    // 存款份额
    asset               total_borrow_shares;                    // 借款份额

    // 指数
    asset               interest_realized;                      // 池子真实收到的利息
    asset               interest_claimed;                       // 用户已经提走的利息
    borrow_index_st         borrow_index;                       // 借款指数
    supply_reward_index_st  supply_index;                       // 存款利息指数

    bool paused = false;

    uint64_t primary_key() const { return sym_code.raw(); }

    EOSLIB_SERIALIZE(
        reserve_state,
        (sym_code)(token_contract)
        (max_ltv)(liquidation_threshold)(liquidation_bonus)(reserve_factor)
        (u_opt)(r0)(r_opt)(r_max)(max_rate_step_bp)
        (total_liquidity)(total_debt)
        (total_supply_shares)(total_borrow_shares)(interest_realized)(interest_claimed)
        (borrow_index)(supply_index)
        (paused)
    )
};
using reserves_t = eosio::multi_index<"reserves"_n, reserve_state>;

// 用户仓位
NTBL("positions") position_row {
    uint64_t                id;                             // 仓位 ID
    name                    owner;                          // 用户
    symbol_code             sym_code;                       // 资产

    asset                   supply_shares;                  // 存款份额
    asset                   borrow_shares;                  // 借款份额

    // 用户侧指数锚点
    borrow_index_st           borrow_index;                 // 用户借款锚点
    user_supply_interest_st   supply_interest;              // 用户存款利息状态

    bool collateral = true;                                 // 是否作为抵押

    uint64_t primary_key() const { return id; }
    uint64_t by_owner() const { return owner.value; }

    uint128_t by_owner_reserve() const {
        return (uint128_t(owner.value) << 64) | sym_code.raw();
    }

    EOSLIB_SERIALIZE(
        position_row,
        (id)(owner)(sym_code)
        (supply_shares)(borrow_shares)
        (borrow_index)(supply_interest)
        (collateral)
    )
};

using positions_t = eosio::multi_index<
    "positions"_n, position_row,
    indexed_by<"byowner"_n,
        const_mem_fun<position_row, uint64_t, &position_row::by_owner>>,
    indexed_by<"ownerreserve"_n,
        const_mem_fun<position_row, uint128_t, &position_row::by_owner_reserve>>
>;

} // namespace tychefi