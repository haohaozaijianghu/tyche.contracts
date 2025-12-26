#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/time.hpp>

namespace tychefi {

using namespace eosio;

static constexpr uint64_t RATE_SCALE  = 10'000;          // basis points
static constexpr uint64_t PRICE_SCALE = 10'000;          // price precision
static constexpr uint32_t SECONDS_PER_YEAR = 31'536'000; // 365 days
static constexpr uint64_t MAX_PRICE_CHANGE_BP = 2000; // 单次最大 20%

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

NTBL("global") global_t {
    name        admin;
    bool        paused          = false;    // 全局暂停
    uint32_t    price_ttl_sec   = 300;      // 价格有效期（秒）
    uint64_t    close_factor_bp = 5000;     // 最大单次清算比例
    // ===== v3 emergency =====
    bool     emergency_mode = false;        // 是否进入紧急模式

    uint64_t emergency_bonus_bp = 500;      // +5% -- 紧急额外清算奖励
    uint64_t max_emergency_bonus_bp = 2000; // +20% -- 紧急奖励上限
    uint64_t backstop_min_reserve = 0;      // 协议最小储备要求

    EOSLIB_SERIALIZE( global_t, (admin)(paused)(price_ttl_sec)(close_factor_bp)
                                (emergency_mode)(emergency_bonus_bp)(max_emergency_bonus_bp)(backstop_min_reserve) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;
//链上价格源
NTBL("prices") price_feed {
    symbol_code sym_code;                   // 资产 symbol
    uint64_t    price;                      // 当前价格
    time_point  updated_at;                 // 最近更新时间

    uint64_t primary_key()const { return sym_code.raw(); }
    EOSLIB_SERIALIZE( price_feed, (sym_code)(price)(updated_at))
};
using prices_t = eosio::multi_index<"prices"_n, price_feed>;
//每个资产的池子状态
NTBL("reserves") reserve_state {
    symbol_code sym_code;                   // 资产 symbol
    name        token_contract;             // 对应 token 合约

    // risk params
    uint64_t max_ltv;                       // 最大借款比率
    uint64_t liquidation_threshold;         // 清算阈值
    uint64_t liquidation_bonus;             // 基础清算奖励
    uint64_t reserve_factor;                // 协议抽成比例

    // interest model
    uint64_t u_opt;                         // 最优利用率
    uint64_t r0;                            // 利率下限
    uint64_t r_opt;                         // 最优点利率
    uint64_t r_max;                         // 最大利率
    // ===== V2: rate step cap =====
    uint64_t max_rate_step_bp = 200;        // 每次accrue最多变化 200 bps（2%年化）
    uint64_t last_borrow_rate_bp = 0;       // 上次生效的借款利率（bps）

    // accounting
    asset     total_liquidity;              // 池子总可用资产
    asset     total_debt;                   // 未偿还借款总额
    asset     total_supply_shares;          // 存款份额总量
    asset     total_borrow_shares;          // 借款份额总量
    asset     protocol_reserve;             // 协议累计收入

    time_point last_updated;                // 上次计息时间
    bool       paused = false;              // 单资产暂停

    uint64_t primary_key()const { return sym_code.raw(); }
    EOSLIB_SERIALIZE( reserve_state, (sym_code)(token_contract)(max_ltv) (liquidation_threshold)
                                    (liquidation_bonus)(reserve_factor)(u_opt)
                                    (r0)(r_opt)(r_max)(max_rate_step_bp)(last_borrow_rate_bp)(total_liquidity)
                                    (total_debt)(total_supply_shares)(total_borrow_shares)
                                    (protocol_reserve)(last_updated)(paused) )
};

using reserves_t = eosio::multi_index<"reserves"_n, reserve_state>;
//  用户仓位
NTBL("positions") position_row {
    uint64_t    id;                         // 主键
    name        owner;                      // 用户
    symbol_code sym_code;                   // 资产

    asset supply_shares;                    // 存款份额
    asset borrow_shares;                    // 借款份额
    bool  collateral = true;                // 是否作为抵押

    uint64_t  primary_key()const { return id; }
    uint64_t  by_owner()const { return owner.value; }
    uint128_t by_owner_reserve()const {
        return (uint128_t(owner.value) << 64) | sym_code.raw();
    }
        EOSLIB_SERIALIZE( position_row, (id)(owner)(sym_code)(supply_shares)(borrow_shares)(collateral) )
};

using positions_t = eosio::multi_index<
   "positions"_n, position_row,
   indexed_by<"byowner"_n,
      const_mem_fun<position_row, uint64_t, &position_row::by_owner>>,
   indexed_by<"ownerreserve"_n,
      const_mem_fun<position_row, uint128_t, &position_row::by_owner_reserve>>
>;

} // namespace tychefi