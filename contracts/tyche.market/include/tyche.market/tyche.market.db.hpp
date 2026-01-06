#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/time.hpp>

namespace tychefi {

using namespace eosio;

// =====================================================
// 全局常量
// =====================================================
static constexpr uint64_t RATE_SCALE        = 10'000;                     // 利率基数（bps）
static constexpr uint32_t SECONDS_PER_YEAR  = 31'536'000;                // 年秒数
static constexpr uint64_t MAX_PRICE_CHANGE_BP = 2000;                    // 单次最大价格变动（20%）
static constexpr uint128_t HIGH_PRECISION   = 1'000'000'000'000'000'000ULL; // 1e18
static constexpr symbol USDT_SYM            = symbol("USDT", 6);

// =====================================================
// error code
// =====================================================
enum class err : uint8_t {
    NONE                    = 0,
    RECORD_EXISTING         = 1,
    RECORD_NOT_FOUND        = 2,
    PARAM_ERROR             = 3,
    PAUSED                  = 4,
    NO_AUTH                 = 5,
    NOT_POSITIVE            = 6,
    ACCOUNT_INVALID         = 7,
    OVERSIZED               = 8,
};

#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + std::to_string((int)code) + "]] " + msg); }

#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.market")]]

// =====================================================
// 全局配置（系统级）
// 只存“策略”，不存“状态”
// =====================================================
NTBL("global") global_t {
    name        admin;                     // 协议管理员
    bool        paused = false;             // 全局暂停开关
    uint32_t    price_ttl_sec = 300;        // 价格最大可用时长
    uint64_t    close_factor_bp = 5000;     // 清算最大还款比例
    bool        emergency_mode = false;     // 紧急模式
    uint64_t    emergency_bonus_bp = 500;   // 紧急清算额外奖励
    uint64_t    max_emergency_bonus_bp = 2000; // 紧急奖励上限

    EOSLIB_SERIALIZE(
        global_t,
        (admin)(paused)(price_ttl_sec)
        (close_factor_bp)
        (emergency_mode)
        (emergency_bonus_bp)
        (max_emergency_bonus_bp)
    )
};
using global_singleton = singleton<"global"_n, global_t>;

// =====================================================
// 价格表（USDT 计价）
// =====================================================
NTBL("prices") price_feed {
    symbol_code sym_code;          // 被定价资产
    asset       price;             // USDT 报价（必须是 USDT_SYM）
    time_point  updated_at;        // 最近更新时间

    uint64_t primary_key() const { return sym_code.raw(); }

    EOSLIB_SERIALIZE(price_feed, (sym_code)(price)(updated_at))
};
using prices_t = multi_index<"prices"_n, price_feed>;

// =====================================================
// 借款指数（池子级）
// =====================================================
struct borrow_index_st {
    uint64_t        id = 0;                     // 单调递增版本号
    uint128_t       index = HIGH_PRECISION;     // 累计倍率（1e18 起）
    uint64_t        borrow_rate_bp = 0;         // 当前年化借款利率
    time_point_sec  last_updated;               // 上次 accrue 时间
};

// =====================================================
// 存款利息指数（池子级）
// =====================================================
struct supply_reward_index_st {
    uint64_t       id = 0;                      // 单调递增版本号
    uint128_t      reward_per_share = 0;        // 每份 share 可得利息 ×1e18
    uint64_t       indexed_available = 0;       // 已被 index 记账过的可分配利息（现金）
    time_point_sec last_updated;                // 上次分配时间
};

// =====================================================
// 用户存款利息（用户级）
// =====================================================
struct user_supply_interest_st {
    uint64_t  id = 0;                           // 用户上次结算时的 id
    uint128_t last_reward_per_share = 0;        // 用户利息锚点
    asset     pending_interest;                 // 已产生但未领取利息
    asset     claimed_interest;                 // 历史已领取利息
};

// =====================================================
// 用户借款状态（核心）
// =====================================================
struct borrow_interest_st {
    uint64_t       id = 0;                      // 单调递增版本号
    uint128_t      last_borrow_index = 0;       // 用户利息结算锚点（borrow_index.index）
    int64_t        accrued_interest  = 0;       // 已产生但未偿还利息（真实 token）
    int128_t       borrow_scaled     = 0;       // 借款本金（不含利息）
    time_point_sec last_updated;                // 最近一次 borrow / repay 时间
};

struct repay_result {
    int64_t   paid         = 0;                 // 实际用于还债的 token 数量（利息+本金）
    int64_t   refund       = 0;                 // 多余退回给 payer 的 token 数量

    int64_t   debt_before  = 0;                 // 还款前的总债务（利息 + 本金）
    int128_t  scaled_delta = 0;                 // 本次实际减少的 borrow_scaled（本金部分）
};

struct liquidate_result {
    int64_t seized = 0;                         // 扣走的抵押资产 amount（collateral token）
    int64_t paid   = 0;                         // 实际用于还债的 amount（debt token）
    int64_t refund = 0;                         // 多余退回的 amount（debt token）
};

struct position_change {
    int128_t borrow_scaled_delta = 0;           // + borrow (scaled)
    int128_t supply_shares_delta = 0;           // +/- supply shares delta

    std::optional<bool> collateral_override;    // 三态：unset / true / false
};

// =====================================================
// Reserve（资产池）
// 池子级“总账”
// =====================================================
NTBL("reserves") reserve_state {
    // -------- identity --------
    symbol_code sym_code;                    // 资产符号
    name        token_contract;              // token 合约

    // -------- balances --------
    asset       total_liquidity;             // 池子真实现金
    asset       total_debt;                  // 冗余展示字段（本金 + 利息）
    asset       total_supply_shares;         // 池子级存款总量

    // -------- borrow aggregation --------
    int128_t total_borrow_scaled;           // 所有用户本金之和
    int64_t total_accrued_interest;         // 所有用户未还利息之和
    asset    interest_realized;             // 已回收到池子的利息现金
    asset    interest_claimed;              // 已领取的总利息
    // -------- interest index --------
    borrow_index_st borrow_index;            // 借款指数（倍率）
    supply_reward_index_st supply_index;     // 存款分发指数

    // -------- risk & rate params --------
    uint64_t    max_ltv;
    uint64_t    liquidation_threshold;
    uint64_t    liquidation_bonus;
    uint64_t    reserve_factor;

    uint64_t    u_opt;
    uint64_t    r0;
    uint64_t    r_opt;
    uint64_t    r_max;
    uint64_t    max_rate_step_bp;

    bool        paused = false;

    uint64_t primary_key() const {
        return sym_code.raw();
    }

    EOSLIB_SERIALIZE(
        reserve_state,
        (sym_code)(token_contract)
        (total_liquidity)(total_debt)(total_supply_shares)
        (total_borrow_scaled)(total_accrued_interest)(interest_realized)(interest_claimed)
        (borrow_index)(supply_index)
        (max_ltv)(liquidation_threshold)(liquidation_bonus)(reserve_factor)
        (u_opt)(r0)(r_opt)(r_max)(max_rate_step_bp)
        (paused)
    )
};
using reserves_t = multi_index<"reserves"_n, reserve_state>;

// =====================================================
// 用户仓位（scope = owner）
// 一个 position = 用户 × 资产
// =====================================================
NTBL("positions") position_row {
    symbol_code             sym_code;           // 资产
    asset                   supply_shares;      // 存款份额
    borrow_interest_st      borrow;             // 借款状态（本金 + 利息）
    user_supply_interest_st supply_interest;    // 存款利息状态
    bool                    collateral = true;  // 是否计入抵押

    uint64_t primary_key() const { return sym_code.raw(); }

    EOSLIB_SERIALIZE(position_row,(sym_code)(supply_shares)(borrow)(supply_interest)(collateral))
};
using positions_t = multi_index<"positions"_n, position_row>;

} // namespace tychefi