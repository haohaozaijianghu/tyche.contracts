#include <tyche.market/tyche.market.hpp>

#include <cmath>
#include <limits>
#include <tuple>
#include "flon.token.hpp"
#include "utils.hpp"
#include "tyche.market.hpp"

namespace tychefi {

static constexpr int128_t MAX_RATE_DELTA = static_cast<int128_t>(std::numeric_limits<int64_t>::max());

static int128_t pow10_i128(uint8_t p) {
      int128_t x = 1;
      for (uint8_t i = 0; i < p; ++i) x *= 10;
      return x;
}

// 价格估值：token_amt * price（都按各自精度还原） => quote 的“最小单位”整数
static int128_t value_of(const asset& token_amt, const asset& price) {
      check(token_amt.amount >= 0, "value_of: token_amt must be non-negative");
      check(price.amount > 0, "value_of: price must be positive");
      return ( (int128_t)token_amt.amount * (int128_t)price.amount )
            / pow10_i128(token_amt.symbol.precision())
            / pow10_i128(price.symbol.precision());
}

void tyche_market::init(name admin) {
      require_auth(get_self());
      CHECKC(is_account(admin), err::ACCOUNT_INVALID, "admin not exist");
      _gstate.admin = admin;

}

void tyche_market::setpause(bool paused) {
      require_auth(_gstate.admin);
      _gstate.paused = paused;
}

void tyche_market::setpricettl(uint32_t ttl_sec) {
      require_auth(_gstate.admin);
      CHECKC(ttl_sec > 0, err::NOT_POSITIVE, "ttl must be positive");
      _gstate.price_ttl_sec = ttl_sec;
}

void tyche_market::setclosefac(uint64_t close_factor_bp) {
      require_auth(_gstate.admin);
      CHECKC(close_factor_bp > 0 && close_factor_bp <= RATE_SCALE, err::NOT_STARTED, "close factor must be within 0-100% in bps");
      _gstate.close_factor_bp = close_factor_bp;
}

void tyche_market::setprice(symbol_code sym, asset price) {
      require_auth(_gstate.admin);
      check(!_gstate.paused, "market paused");
      check(sym.is_valid(), "invalid symbol");
      check(price.amount > 0, "price must be positive");

      // 1) reserve 必须存在（定价白名单）
      reserves_t reserves(get_self(), get_self().value);
      auto res_itr = reserves.find(sym.raw());
      check(res_itr != reserves.end(), "reserve not found");

      // 2) 价格符号必须匹配（避免出现“USDT 价格写成 BTC 符号”的脏数据）
      // 你当前 price_feed.price 是 asset，symbol 允许你自定义为“报价币种”（例如 USDT）
      // 但至少要保证精度一致/固定。这里做最小约束：symbol 必须有效 & amount > 0
      check(price.symbol.is_valid(), "invalid price symbol");

      // 3) 单次最大变动限制（可选，但你 db.hpp 里定义了 MAX_PRICE_CHANGE_BP，建议启用）
      prices_t prices(get_self(), get_self().value);
      auto price_itr = prices.find(sym.raw());
      auto now = current_time_point();

      if (price_itr != prices.end()) {
         // 旧价存在：限制单次跳变
         // change_bp = |new-old| / old
         int128_t oldp = (int128_t)price_itr->price.amount;
         int128_t newp = (int128_t)price.amount;
         check(oldp > 0, "invalid stored old price");

         int128_t diff = (newp > oldp) ? (newp - oldp) : (oldp - newp);
         int128_t change_bp = diff * (int128_t)RATE_SCALE / oldp;

         check(change_bp <= (int128_t)MAX_PRICE_CHANGE_BP,
               "price change too large in one update");
      }

      // 4) 写入 prices 表
      if (price_itr == prices.end()) {
         prices.emplace(get_self(), [&](auto& row) {
            row.sym_code   = sym;
            row.price      = price;
            row.updated_at = now;
         });
      } else {
         prices.modify(price_itr, same_payer, [&](auto& row) {
            row.price      = price;
            row.updated_at = now;
         });
      }
}

void tyche_market::setemergency(bool enabled){
      require_auth(_gstate.admin);
      _gstate.emergency_mode = enabled;
}

void tyche_market::setemcfg(uint64_t bonus_bp, uint64_t max_bonus_bp, uint64_t backstop_min) {
      require_auth(_gstate.admin);

      check(max_bonus_bp <= RATE_SCALE, "max_bonus_bp must be <= 10000");
      check(bonus_bp <= max_bonus_bp, "bonus exceeds max");

      _gstate.emergency_bonus_bp     = bonus_bp;
      _gstate.max_emergency_bonus_bp = max_bonus_bp;
      _gstate.backstop_min_reserve   = backstop_min;
}

void tyche_market::setreserve(const symbol_code sym,
                              const uint64_t max_ltv,
                              const uint64_t liq_threshold,
                              const uint64_t liq_bonus,
                              const uint64_t reserve_factor) {
   require_auth(_gstate.admin);
   check(!_gstate.paused, "market paused");

   reserves_t reserves(get_self(), get_self().value);
   auto itr = reserves.find(sym.raw());
   check(itr != reserves.end(), "reserve not found");

   // ========= 风控参数 =========
   check(max_ltv <= RATE_SCALE, "max_ltv too large");

   if (max_ltv > 0) {
      // ---- 可抵押资产 ----
      check(liq_threshold <= RATE_SCALE, "liquidation threshold too large");
      check(liq_threshold >= max_ltv, "liquidation threshold < max ltv");
      check(liq_bonus >= RATE_SCALE && liq_bonus <= RATE_SCALE * 2,"liquidation bonus must be between 1x and 2x");

      // liquidation 数学不变量
      check( (int128_t)liq_threshold * liq_bonus <(int128_t)RATE_SCALE * RATE_SCALE, "invalid liquidation parameters");
   } else {
      // ---- 不可抵押资产（如 USDT）----
      check(liq_threshold == 0, "liq_threshold must be 0 when max_ltv == 0");
   }

   // reserve factor：最多 50%
   check(reserve_factor <= RATE_SCALE / 2, "reserve factor too high");
   _accrue_and_store(reserves, itr);
   auto res = *itr;
   // ❗ 不触碰 borrow_accrual / interest_reward（计息连续性）
   reserves.modify(itr, same_payer, [&](auto& row) {
      row.max_ltv               = max_ltv;
      row.liquidation_threshold = liq_threshold;
      row.liquidation_bonus     = liq_bonus;
      row.reserve_factor        = reserve_factor;
   });
}

void tyche_market::addreserve(const extended_symbol& asset_sym,
                                       const uint64_t& max_ltv,
                                       const uint64_t& liq_threshold,
                                       const uint64_t& liq_bonus,
                                       const uint64_t& reserve_factor,
                                       const uint64_t& u_opt,
                                       const uint64_t& r0,
                                       const uint64_t& r_opt,
                                       const uint64_t& r_max) {
   require_auth(_gstate.admin);
   check(!_gstate.paused, "market paused");

   symbol sym = asset_sym.get_symbol();

   // 1. symbol 校验
   check(sym.is_valid(), "invalid symbol");
   check(sym.precision() <= 8, "token precision too large");

   // 2. 风控参数校验
   check(max_ltv <= RATE_SCALE, "max_ltv too large");

   if (max_ltv > 0) {
      check(liq_threshold <= RATE_SCALE, "liquidation threshold too large");
      check(liq_threshold >= max_ltv, "liq_threshold < max_ltv");

      // liquidation_bonus 用 bp 表示，1.0x ~ 2.0x
      check(liq_bonus >= RATE_SCALE &&liq_bonus <= RATE_SCALE * 2,"liquidation bonus must be between 1x and 2x");

      // 数学安全性约束
      check((int128_t)liq_threshold * liq_bonus < (int128_t)RATE_SCALE * RATE_SCALE,"invalid liquidation parameters");
   } else {
      // 不可抵押资产（如稳定币）
      check(liq_threshold == 0, "liq_threshold must be 0 if max_ltv == 0");
   }

   // 协议抽成最多 50%
   check(reserve_factor <= RATE_SCALE / 2, "reserve factor too high");

   // =====================================================
   // 3. 利率模型校验
   // =====================================================
   check(u_opt > 0 && u_opt < RATE_SCALE, "u_opt must be in (0, 100%)");
   check(r0 <= r_opt && r_opt <= r_max, "invalid interest rate curve");

   // =====================================================
   // 4. reserve 唯一性
   // =====================================================
   reserves_t reserves(get_self(), get_self().value);
   check(reserves.find(sym.code().raw()) == reserves.end(),"reserve already exists");

   // =====================================================
   // 5. 初始化 reserve
   // =====================================================
   auto now = time_point_sec(current_time_point());

   reserves.emplace(get_self(), [&](auto& row) {
      // ---- identity ----
      row.sym_code       = sym.code();
      row.token_contract = asset_sym.get_contract();

      // ---- 风控参数 ----
      row.max_ltv               = max_ltv;
      row.liquidation_threshold = liq_threshold;
      row.liquidation_bonus     = liq_bonus;
      row.reserve_factor        = reserve_factor;

      // ---- 利率模型 ----
      row.u_opt            = u_opt;
      row.r0               = r0;
      row.r_opt            = r_opt;
      row.r_max            = r_max;
      row.max_rate_step_bp = 200;

      // ---- 资金状态（全为 0）----
      row.total_liquidity     = asset(0, sym);
      row.total_debt          = asset(0, sym);
      row.total_supply_shares = asset(0, sym);
      row.total_borrow_shares = asset(0, sym);
      row.interest_realized   = asset(0, sym);
      row.interest_claimed    = asset(0, sym);
      // =================================================
      // 借款指数（borrow_index）
      // =================================================
      row.borrow_index.index_id               = 0;
      row.borrow_index.interest_per_share     = 0;     // 从 0 开始
      row.borrow_index.borrow_rate_bp         = r0;    // 初始利率
      row.borrow_index.last_borrow_rate_bp    = r0;
      row.borrow_index.last_updated           = now;

      // =================================================
      // 存款利息指数（supply_index）
      // =================================================
      row.supply_index.index_id            = 0;
      row.supply_index.reward_per_share    = 0;        // 从 0 开始
      row.supply_index.last_updated        = now;

      row.paused = false;
   });
}

void tyche_market::_on_supply(const name& owner, const asset& quantity) {
    check(quantity.amount > 0, "quantity must be positive");

    reserves_t reserves(get_self(), get_self().value);
    auto res_itr = reserves.find(quantity.symbol.code().raw());
    check(res_itr != reserves.end(), "reserve not found");
    check(!res_itr->paused, "reserve paused");

    if (res_itr->max_ltv > 0 || res_itr->liquidation_threshold > 0) {
        _check_price_available(quantity.symbol.code());
    }

    // 1️⃣ 先推进池子指数
    _accrue_and_store(reserves, res_itr);
    res_itr = reserves.find(quantity.symbol.code().raw());
    auto res = *res_itr;

    // 2️⃣ 算 shares
    asset shares = _supply_shares_from_amount(quantity,res.total_supply_shares,res.total_liquidity);
    check(shares.amount > 0, "supply amount too small");

    positions_t positions(get_self(), get_self().value);
    auto pos_ptr = _get_or_create_position(positions, owner, quantity.symbol.code(), quantity);

    const bool can_be_collateral =
        (res.max_ltv > 0 && res.liquidation_threshold > 0);

    positions.modify(*pos_ptr, same_payer, [&](auto& row) {
        // 先结算历史利息
        _settle_supply_interest(row, res);

        bool first_supply = (row.supply_shares.amount == 0);
        row.supply_shares += shares;

        if (can_be_collateral && first_supply) {
            row.collateral = true;
        }
    });

    // 3️⃣ 更新池子现金与份额
    reserves.modify(res_itr, same_payer, [&](auto& row) {
        row.total_liquidity     += quantity;
        row.total_supply_shares += shares;
    });
}
void tyche_market::withdraw(name owner, asset quantity) {
    require_auth(owner);
    check(!_gstate.paused, "market paused");
    check(quantity.amount > 0, "quantity must be positive");

    // ===== reserve =====
    reserves_t reserves(get_self(), get_self().value);
    auto res_itr = reserves.find(quantity.symbol.code().raw());
    check(res_itr != reserves.end(), "reserve not found");
    check(!res_itr->paused, "reserve paused");

    if (res_itr->max_ltv > 0 || res_itr->liquidation_threshold > 0) {
        _check_price_available(quantity.symbol.code());
    }

    // ===== position =====
    positions_t positions(get_self(), get_self().value);
    auto owner_idx = positions.get_index<"ownerreserve"_n>();
    auto pos_itr =
        owner_idx.find((uint128_t(owner.value) << 64) | quantity.symbol.code().raw());

    check(pos_itr != owner_idx.end(), "no position found");
    check(pos_itr->supply_shares.amount > 0, "no supply shares");

    // =====================================================
    // 1️⃣ 推进池子指数
    // =====================================================
    _accrue_and_store(reserves, res_itr);

    res_itr = reserves.find(quantity.symbol.code().raw());
    auto res = *res_itr;

    // =====================================================
    // 2️⃣ 结算用户应计利息（pending_interest）
    // =====================================================
    owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
        _settle_supply_interest(row, res);
    });

    // 重新读 position（避免引用旧值）
    pos_itr = owner_idx.find((uint128_t(owner.value) << 64) | quantity.symbol.code().raw());

    // =====================================================
    // 3️⃣ 自动 claim 利息（现金约束）
    // =====================================================
    asset available_interest = res.interest_realized - res.interest_claimed;

    asset claimable(0, quantity.symbol);

    if (available_interest.amount > 0 &&
        pos_itr->supply_interest.pending_interest.amount > 0) {

        int64_t claim_amt = std::min( pos_itr->supply_interest.pending_interest.amount, available_interest.amount);

        if (claim_amt > 0) {
            claimable = asset(claim_amt, quantity.symbol);

            // 转账利息
            _transfer_out(res.token_contract, owner, claimable, "claim interest");

            // 写回 position
            owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
                row.supply_interest.pending_interest.amount -= claim_amt;
                row.supply_interest.claimed_interest        += claimable;
            });

            // 写回 reserve（现金真的出去）
            reserves.modify(res_itr, same_payer, [&](auto& row) {
                row.interest_claimed += claimable;
                row.total_liquidity  -= claimable;
            });

            // 刷新 res
            res_itr = reserves.find(quantity.symbol.code().raw());
            res = *res_itr;
        }
    }

    // =====================================================
    // 4️⃣ 计算可提本金
    // =====================================================
    asset max_withdrawable = _amount_from_shares(pos_itr->supply_shares,res.total_supply_shares,res.total_liquidity );

    check(quantity <= max_withdrawable, "withdraw exceeds balance");
    check(quantity.amount <= res.total_liquidity.amount, "insufficient liquidity");

    asset share_delta = _withdraw_shares_from_amount( quantity,res.total_supply_shares,res.total_liquidity);

    // =====================================================
    // 5️⃣ 风控模拟（仅 collateral）
    // =====================================================
    if (pos_itr->collateral) {
        owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
            row.supply_shares -= share_delta;
        });

        valuation v = _compute_valuation(owner);
        check(v.debt_value == 0 || v.collateral_value >= v.debt_value, "health factor below 1 after withdraw");

        // rollback
        owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
            row.supply_shares += share_delta;
        });
    }

    // =====================================================
    // 6️⃣ 转账本金
    // =====================================================
    _transfer_out(res.token_contract, owner, quantity, "withdraw");

    // =====================================================
    // 7️⃣ 提交仓位
    // =====================================================
    owner_idx.modify(pos_itr, owner, [&](auto& row) {
        row.supply_shares -= share_delta;
        if (row.supply_shares.amount == 0) {
            row.collateral = false;
        }
    });

    // =====================================================
    // 8️⃣ 更新池子
    // =====================================================
    reserves.modify(res_itr, same_payer, [&](auto& row) {
        row.total_liquidity     -= quantity;
        row.total_supply_shares -= share_delta;
    });
}

void tyche_market::claimint(name owner, symbol_code sym) {
    require_auth(owner);
    check(!_gstate.paused, "market paused");

    // ===== reserve =====
    reserves_t reserves(get_self(), get_self().value);
    auto res_itr = reserves.find(sym.raw());
    check(res_itr != reserves.end(), "reserve not found");
    check(!res_itr->paused, "reserve paused");

    // 推进池子指数（不动现金）
    _accrue_and_store(reserves, res_itr);

    res_itr = reserves.find(sym.raw());
    auto res = *res_itr;

    // ===== position =====
    positions_t positions(get_self(), get_self().value);
    auto owner_idx = positions.get_index<"ownerreserve"_n>();
    auto pos_itr = owner_idx.find((uint128_t(owner.value) << 64) | sym.raw());

    check(pos_itr != owner_idx.end(), "no position");
    check(pos_itr->supply_shares.amount > 0, "no supply");

    auto& user = pos_itr->supply_interest;
    auto& pool = res.supply_index;

    // ===== 双重锚点 ①：指数锚点 =====
    int128_t delta_rps =(int128_t)pool.reward_per_share - (int128_t)user.last_reward_per_share;

    check(delta_rps > 0, "no interest accrued");

    int128_t theoretical_interest = delta_rps * (int128_t)pos_itr->supply_shares.amount / (int128_t)HIGH_PRECISION;

    check(theoretical_interest > 0, "interest too small");

    // ===== 双重锚点 ②：现金锚点 =====
    asset available =
        res.interest_realized - res.interest_claimed;

    check(available.amount > 0, "no interest available");

    int64_t claim_amt =
        std::min<int128_t>(theoretical_interest, available.amount);

    check(claim_amt > 0, "claim amount zero");

    asset claim_asset(claim_amt, available.symbol);

    // ===== 转账 =====
    _transfer_out(res.token_contract, owner, claim_asset, "claim interest");

    // ===== 写回 position =====
    owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
        row.supply_interest.last_reward_per_share = pool.reward_per_share;
        row.supply_interest.claimed_interest += claim_asset;
    });

    // ===== 写回 reserve =====
    reserves.modify(res_itr, same_payer, [&](auto& row) {
        row.interest_claimed += claim_asset;
        row.total_liquidity  -= claim_asset; // 🔴 现金真的出去
    });
}


void tyche_market::setcollat(name owner, symbol_code sym, bool enabled) {
   require_auth(owner);
   check(!_gstate.paused, "market paused");

   reserves_t reserves(get_self(), get_self().value);
   auto res_itr = reserves.find(sym.raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   positions_t positions(get_self(), get_self().value);
   auto owner_idx = positions.get_index<"ownerreserve"_n>();
   auto pos_itr   = owner_idx.find((uint128_t(owner.value) << 64) | sym.raw());
   check(pos_itr != owner_idx.end(), "no position found");

   // 不允许对空仓位开启抵押
   check(!enabled || pos_itr->supply_shares.amount > 0,
         "cannot enable collateral with zero supply");

   // 资产是否允许作为抵押（按你当前语义：max_ltv==0 就禁止抵押）
   check(!enabled || res_itr->max_ltv > 0,
         "asset cannot be used as collateral");

   if (pos_itr->collateral == enabled) return;

   // 开启/关闭 collateral 都会影响估值（至少当 owner 有 debt 时）
   // 开启时：必须要价格可用（避免开启后 valuation 读不到价）
   if (enabled) {
      _check_price_available(sym);
   }

   // 如果 owner 没有 debt，关闭 collateral 永远安全；开启也只是打标
   // 但为了保持规则统一，我们只在“会导致 HF<1”的情况下阻止
   // => 简化：只要 owner 有 debt，就做模拟校验
   valuation before = _compute_valuation(owner);

   if (before.debt_value > 0) {
      owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
         row.collateral = enabled;
      });

      valuation after = _compute_valuation(owner);
      check(after.collateral_value >= after.debt_value,
            "health factor below 1 after collateral change");

      // rollback
      owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
         row.collateral = !enabled;
      });
   }

   // commit
   owner_idx.modify(pos_itr, owner, [&](auto& row) {
      row.collateral = enabled;
   });
}

void tyche_market::borrow(name owner, asset quantity) {
   require_auth(owner);
   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   // 借款资产必须允许借（max_ltv > 0）
   check(res_itr->max_ltv > 0, "borrowing disabled for this asset");

   // 债务资产价格必须可用
   _check_price_available(quantity.symbol.code());

   positions_t positions(get_self(), get_self().value);

   // ===== accrue borrower 相关的所有 reserve（去重）=====
   std::set<uint64_t> touched;
   auto byowner = positions.get_index<"byowner"_n>();
   for (auto it = byowner.lower_bound(owner.value);
        it != byowner.end() && it->owner == owner; ++it) {
      uint64_t key = it->sym_code.raw();
      if (touched.insert(key).second) {
         auto ritr = reserves.find(key);
         if (ritr != reserves.end()) {
            _accrue_and_store(reserves, ritr);
         }
      }
   }

   // 重新获取当前 reserve（已 accrue）
   res_itr = reserves.find(quantity.symbol.code().raw());
   auto res = *res_itr;

   // ===== cash semantics：池子必须有足够流动性 =====
   check(quantity.amount <= res.total_liquidity.amount, "insufficient liquidity");

   // 获取 / 创建 position
   auto pos_ptr = _get_or_create_position(
      positions, owner, quantity.symbol.code(), quantity
   );

   // 计算 borrow shares（ceil）
   asset borrow_shares = _borrow_shares_from_amount(
      quantity, res.total_borrow_shares, res.total_debt
   );
   check(borrow_shares.amount > 0, "borrow amount too small");

   // 溢出护栏
   check(res.total_debt.amount <= std::numeric_limits<int64_t>::max() - quantity.amount,
         "debt overflow");
   check(res.total_borrow_shares.amount <=
         std::numeric_limits<int64_t>::max() - borrow_shares.amount,
         "borrow shares overflow");

   // ===== 模拟写入，用于风控 =====
   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.borrow_shares += borrow_shares;
   });

   valuation val = _compute_valuation(owner);
   check(val.max_borrowable_value > 0, "no collateral enabled");
   check(val.debt_value <= val.max_borrowable_value, "exceeds max LTV");
   check(val.collateral_value >= val.debt_value,
         "health factor below 1 after borrow");

   // rollback
   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.borrow_shares -= borrow_shares;
   });

   // ===== 实际放款 =====
   _transfer_out(res.token_contract, owner, quantity, "borrow");

   // ===== commit position =====
   positions.modify(*pos_ptr, owner, [&](auto& row) {
      row.borrow_shares += borrow_shares;
   });

   // ===== commit reserve（只改本动作影响的字段）=====
   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_debt          += quantity;
      row.total_borrow_shares += borrow_shares;
      row.total_liquidity     -= quantity;   // cash semantics
   });
}

void tyche_market::_on_repay(
    const name& payer,
    const name& borrower,
    const asset& quantity
) {
    // ================= 基础校验 =================
    check(quantity.amount > 0, "repay amount must be positive");
    check(is_account(borrower), "invalid borrower");

    // ================= reserve =================
    reserves_t reserves(get_self(), get_self().value);
    auto res_itr = reserves.find(quantity.symbol.code().raw());
    check(res_itr != reserves.end(), "reserve not found");
    check(!res_itr->paused, "reserve paused");

    // token 合约必须匹配
    check(res_itr->token_contract == get_first_receiver(),
          "invalid token contract");

    // ================= borrower position =================
    positions_t positions(get_self(), get_self().value);
    auto owner_idx = positions.get_index<"ownerreserve"_n>();
    auto pos_itr =
        owner_idx.find((uint128_t(borrower.value) << 64)
                        | quantity.symbol.code().raw());

    check(pos_itr != owner_idx.end(), "no borrow position");
    check(pos_itr->borrow_shares.amount > 0, "no debt to repay");

    // ================= accrue（推进指数） =================
    _accrue_and_store(reserves, res_itr);

    // 刷新快照
    res_itr = reserves.find(quantity.symbol.code().raw());
    auto res = *res_itr;

    // ================= 当前债务（含利息） =================
    asset current_debt = _amount_from_shares(
        pos_itr->borrow_shares,
        res.total_borrow_shares,
        res.total_debt
    );
    check(current_debt.amount > 0, "no outstanding debt");

    // clamp repay
    asset repay = quantity;
    if (repay > current_debt) {
        repay = current_debt;
    }
    check(repay.amount > 0, "repay too small");

    // ================= 计算 shares delta =================
    asset share_delta = _repay_shares_from_amount(
        repay,
        res.total_borrow_shares,
        res.total_debt
    );

    // 全额还清兜底
    if (repay == current_debt) {
        share_delta = pos_itr->borrow_shares;
    }

    check(share_delta.amount > 0, "repay too small");
    check(share_delta.amount <= pos_itr->borrow_shares.amount,
          "repay exceeds borrow shares");

    // ================= 拆分：先还利息，再还本金 =================
    // 估算该用户当前“应计利息”
    int128_t theoretical_principal =
        (int128_t)res.total_debt.amount
        * (int128_t)pos_itr->borrow_shares.amount
        / (int128_t)res.total_borrow_shares.amount;

    int128_t interest_outstanding =
        (int128_t)current_debt.amount - theoretical_principal;

    if (interest_outstanding < 0) interest_outstanding = 0;

    int128_t interest_pay =
        std::min<int128_t>(interest_outstanding, repay.amount);

    int128_t principal_pay =
        (int128_t)repay.amount - interest_pay;

    asset interest_repaid(
        (int64_t)interest_pay,
        repay.symbol
    );
    asset principal_repaid(
        (int64_t)principal_pay,
        repay.symbol
    );

    // ================= 更新 borrower 仓位 =================
    owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
        row.borrow_shares -= share_delta;
    });

    // ================= 更新 reserve =================
    reserves.modify(res_itr, same_payer, [&](auto& row) {
        // 本金才会减少 total_debt
        row.total_debt          -= principal_repaid;
        row.total_borrow_shares -= share_delta;

        // 现金：本金 + 利息 都进入池子
        row.total_liquidity     += repay;

        // 🔴 真实收到的利息（关键修复点）
        if (interest_repaid.amount > 0) {
            row.interest_realized += interest_repaid;
        }
    });
}

void tyche_market::_on_liquidate(name liquidator,name borrower,symbol_code debt_sym,asset repay_amount,symbol_code collateral_sym) {
   require_auth(liquidator);
   check(!_gstate.paused, "market paused");
   check(liquidator != borrower, "self liquidation not allowed");
   check(repay_amount.amount > 0, "repay amount must be positive");
   check(debt_sym != collateral_sym, "invalid liquidation asset");

   asset paid_in = repay_amount; // 原始转入

   // ---------- reserves ----------
   reserves_t reserves(get_self(), get_self().value);
   auto debt_itr = reserves.find(debt_sym.raw());
   auto coll_itr = reserves.find(collateral_sym.raw());

   check(debt_itr != reserves.end(), "debt reserve not found");
   check(coll_itr != reserves.end(), "collateral reserve not found");
   check(!debt_itr->paused, "debt reserve paused");
   check(!coll_itr->paused, "collateral reserve paused");

   // token 来源校验（本次 liquidate 是通过 on_transfer 触发）
   check(get_first_receiver() == debt_itr->token_contract, "invalid debt token contract");
   check(repay_amount.symbol.code() == debt_sym, "repay symbol mismatch");

   // ---------- positions ----------
   positions_t positions(get_self(), get_self().value);
   auto owner_idx = positions.get_index<"ownerreserve"_n>();

   auto debt_pos = owner_idx.find(((uint128_t)borrower.value << 64) | debt_sym.raw());
   auto coll_pos = owner_idx.find(((uint128_t)borrower.value << 64) | collateral_sym.raw());

   check(debt_pos != owner_idx.end(), "borrow position not found");
   check(coll_pos != owner_idx.end(), "collateral position not found");
   check(debt_pos->borrow_shares.amount > 0, "no outstanding debt");
   check(coll_pos->supply_shares.amount > 0, "no collateral supplied");
   check(coll_pos->collateral, "asset not enabled as collateral");

   // ---------- accrue ALL borrower reserves ----------
   auto byowner = positions.get_index<"byowner"_n>();
   for (auto it = byowner.lower_bound(borrower.value); it != byowner.end() && it->owner == borrower; ++it) {
      auto ritr = reserves.find(it->sym_code.raw());
      if (ritr != reserves.end()) _accrue_and_store(reserves, ritr);
   }

   // refresh
   debt_itr = reserves.find(debt_sym.raw());
   coll_itr = reserves.find(collateral_sym.raw());
   auto debt_res = *debt_itr;
   auto coll_res = *coll_itr;

   // ---------- eligibility ----------
   valuation val = _compute_valuation(borrower);
   check(val.debt_value > 0, "no debt");
   check(val.collateral_value < val.debt_value, "position not eligible for liquidation");

   // ---------- current debt ----------
   asset borrower_debt = _amount_from_shares(
      debt_pos->borrow_shares,
      debt_res.total_borrow_shares,
      debt_res.total_debt
   );
   check(borrower_debt.amount > 0, "no outstanding debt");
   if (repay_amount > borrower_debt) repay_amount = borrower_debt;

   // ---------- prices (asset) ----------
   prices_t prices(get_self(), get_self().value);
   asset debt_price_asset = _get_fresh_price(prices, debt_sym);
   asset coll_price_asset = _get_fresh_price(prices, collateral_sym);

   // ---------- values (quote minimal unit) ----------
   int128_t debt_value  = value_of(borrower_debt, debt_price_asset);
   int128_t repay_value = value_of(repay_amount,  debt_price_asset);

   // ---------- close factor cap ----------
   int128_t max_repay_close = debt_value * _gstate.close_factor_bp / RATE_SCALE;

   // ---------- HF -> 1 cap ----------
   int128_t shortfall = val.debt_value - val.collateral_value; // >0
   int128_t denom =
      (int128_t)RATE_SCALE * RATE_SCALE
      - (int128_t)coll_res.liquidation_threshold * coll_res.liquidation_bonus;

   int128_t max_repay_to_one = max_repay_close;
   if (denom > 0) {
      max_repay_to_one =
         (shortfall * (int128_t)RATE_SCALE * RATE_SCALE + denom - 1) / denom;
   }

   // ---------- final repay value cap ----------
   int128_t repay_value_cap =
      std::min<int128_t>(repay_value, std::min(max_repay_close, max_repay_to_one));
   check(repay_value_cap > 0, "repay amount too small");

   // ---------- value -> debt amount (ceil) ----------
   // repay_value_cap 的单位：quote 的最小单位（由 value_of 定义）
   // repay_amount.amount 的单位：debt token 最小单位
   // 反推：amount = ceil( repay_value_cap * 10^debt_precision * 10^price_precision / price_amount )
   {
      int128_t num =
         repay_value_cap
         * pow10_i128(repay_amount.symbol.precision())
         * pow10_i128(debt_price_asset.symbol.precision());

      int128_t den = (int128_t)debt_price_asset.amount;
      check(den > 0, "invalid debt price");

      int64_t capped_amt = (int64_t)((num + den - 1) / den);
      check(capped_amt > 0, "repay amount too small");

      repay_amount = asset(capped_amt, paid_in.symbol);
      if (repay_amount > borrower_debt) repay_amount = borrower_debt;
   }

   // ---------- refund extra paid_in ----------
   asset refund = paid_in - repay_amount;
   if (refund.amount > 0) {
      _transfer_out(debt_res.token_contract, liquidator, refund, "liquidate refund");
   }

   // ---------- recompute repay_value after cap ----------
   repay_value = value_of(repay_amount, debt_price_asset);
   check(repay_value > 0, "repay amount too small");

   // ---------- repay shares ----------
   asset debt_share_delta = _repay_shares_from_amount(
      repay_amount,
      debt_res.total_borrow_shares,
      debt_res.total_debt
   );
   check(debt_share_delta.amount > 0, "repay too small");

   // ---------- liquidation bonus ----------
   uint64_t bonus_bp = coll_res.liquidation_bonus;
   if (_gstate.emergency_mode) {
      uint64_t max_bonus = RATE_SCALE + _gstate.max_emergency_bonus_bp;
      bonus_bp = std::min<uint64_t>(max_bonus, bonus_bp + _gstate.emergency_bonus_bp);
   }

   // repay_value(quote) -> seize_value(quote)
   int128_t seize_value = repay_value * (int128_t)bonus_bp / RATE_SCALE;
   check(seize_value > 0, "seize value zero");

   // ---------- seize_value(quote) -> collateral amount (floor) ----------
   // amount = floor( seize_value * 10^coll_precision * 10^price_precision / price_amount )
   int128_t seize_amt_128 =
      seize_value
      * pow10_i128(coll_res.total_liquidity.symbol.precision())
      * pow10_i128(coll_price_asset.symbol.precision())
      / (int128_t)coll_price_asset.amount;

   check(seize_amt_128 > 0, "seize amount zero");
   check(seize_amt_128 <= (int128_t)std::numeric_limits<int64_t>::max(), "seize overflow");

   asset seize_asset((int64_t)seize_amt_128, coll_res.total_liquidity.symbol);

   // ---------- collateral balance ----------
   asset collateral_balance = _amount_from_shares(
      coll_pos->supply_shares,
      coll_res.total_supply_shares,
      coll_res.total_liquidity
   );
   check(collateral_balance.amount >= seize_asset.amount, "insufficient collateral");

   asset coll_share_delta = _withdraw_shares_from_amount(
      seize_asset,
      coll_res.total_supply_shares,
      coll_res.total_liquidity
   );
   check(coll_share_delta.amount > 0, "seize too small");
   check(coll_share_delta.amount <= coll_pos->supply_shares.amount, "seize exceeds collateral shares");

   // ---------- payout collateral ----------
   _transfer_out(coll_res.token_contract, liquidator, seize_asset, "liquidate seize");

   // ---------- write positions ----------
   owner_idx.modify(debt_pos, _self, [&](auto& row) {
      row.borrow_shares -= debt_share_delta;
   });
   owner_idx.modify(coll_pos, _self, [&](auto& row) {
      _settle_supply_interest(row, coll_res);
      row.supply_shares -= coll_share_delta;
   });

   // ---------- write reserves ----------
   reserves.modify(debt_itr, _self, [&](auto& row) {
      row.total_debt          = debt_res.total_debt - repay_amount;
      row.total_borrow_shares = debt_res.total_borrow_shares - debt_share_delta;
      row.total_liquidity     = debt_res.total_liquidity + repay_amount;

      // 下面这些字段：如果你 struct 里没有，就删掉；要以你 reserve_state 为准
      // row.last_updated        = debt_res.last_updated;
      // row.protocol_reserve    = debt_res.protocol_reserve;
      // row.total_supply_shares = debt_res.total_supply_shares;
   });

   reserves.modify(coll_itr, _self, [&](auto& row) {
      row.total_liquidity     = coll_res.total_liquidity - seize_asset;
      row.total_supply_shares = coll_res.total_supply_shares - coll_share_delta;

      // 下面这些字段：如果你 struct 里没有，就删掉；要以你 reserve_state 为准
      // row.last_updated        = coll_res.last_updated;
      // row.protocol_reserve    = coll_res.protocol_reserve;
      // row.total_debt          = coll_res.total_debt;
      // row.total_borrow_shares = coll_res.total_borrow_shares;
   });
}

void tyche_market::on_transfer(const name& from,const name& to,const asset& quantity,const string& memo) {
   if (to != get_self()) return;
   if (from == get_self()) return;

   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   auto parts = split(memo, ":");
   check(parts.size() >= 1, "invalid memo");

   const string& cmd = parts[0];

   // ---- supply ----
   if (cmd == "supply") {
      check(parts.size() == 1, "invalid supply memo");
      _on_supply(from, quantity);
      return;
   }

   // ---- repay ----
   if (cmd == "repay") {
      // repay:borrower
      check(parts.size() == 2, "invalid repay memo");
      name borrower = name(parts[1]);
      check(is_account(borrower), "invalid borrower");
      _on_repay(from, borrower, quantity);
      return;
   }

   // ---- liquidate ----
   if (cmd == "liquidate") {
      // liquidate:borrower:DEBT:COLL
      check(parts.size() == 4, "invalid liquidate memo");

      name borrower = name(parts[1]);
      symbol_code debt_sym(parts[2]);
      symbol_code coll_sym(parts[3]);

      _on_liquidate(
         from,        // liquidator
         borrower,
         debt_sym,
         quantity,    // repay_amount
         coll_sym
      );
      return;
   }

   check(false, "invalid memo command");
}

reserve_state tyche_market::_require_reserve(const symbol &sym)
{
    reserves_t reserves(get_self(), get_self().value);
    auto itr = reserves.find(sym.code().raw());
    check(itr != reserves.end(), "reserve not found");
    return *itr;
}

static inline int64_t mul_div_i128_to_i64(int64_t a, int128_t b, int128_t den) {
   int128_t x = (int128_t)a * b;
   x /= den;
   if (x <= 0) return 0;
   check(x <= (int128_t)std::numeric_limits<int64_t>::max(), "mul_div overflow");
   return (int64_t)x;
}


void tyche_market::_accrue_inplace(reserve_state& res, time_point_sec now) {
    auto& bidx = res.borrow_index;
    auto& sidx = res.supply_index;

    if (now <= bidx.last_updated) return;

    uint32_t elapsed =
        now.sec_since_epoch() - bidx.last_updated.sec_since_epoch();

    if (elapsed == 0) {
        bidx.last_updated = now;
        return;
    }

    // ===== 无债务：只更新时间，不滚指数 =====
    if (res.total_debt.amount <= 0 || res.total_borrow_shares.amount <= 0) {
        bidx.borrow_rate_bp = res.r0;
        bidx.last_updated  = now;
        return;
    }

    // ===== 计算利率 =====
    uint64_t util = _util_bps(res);
    uint64_t rate = (uint64_t)_calc_borrow_rate(res, util);
    bidx.borrow_rate_bp = rate;

    // Δborrow_rps = rate * dt / year
    int128_t delta_borrow_rps = (int128_t)rate * elapsed * HIGH_PRECISION / (int128_t)(RATE_SCALE * SECONDS_PER_YEAR);

    if (delta_borrow_rps > 0) {
        bidx.interest_per_share += delta_borrow_rps;

        // ===== 同步存款指数 =====
        int128_t total_interest =(int128_t)res.total_debt.amount * rate * elapsed / (int128_t)(RATE_SCALE * SECONDS_PER_YEAR);

        if (total_interest > 0 && res.total_supply_shares.amount > 0) {
            int128_t supplier_part = total_interest * (RATE_SCALE - res.reserve_factor) / RATE_SCALE;

            int128_t delta_supply_rps = supplier_part * HIGH_PRECISION / res.total_supply_shares.amount;

            if (delta_supply_rps > 0) {
                sidx.reward_per_share += delta_supply_rps;
                sidx.index_id += 1;
            }
        }
    }

    bidx.last_updated = now;
}

void tyche_market::_accrue_and_store(reserves_t& reserves, reserves_t::const_iterator itr) {
    auto now = time_point_sec(current_time_point());
    reserves.modify(itr, same_payer, [&](auto& row) {
        _accrue_inplace(row, now);
    });
}

uint64_t tyche_market::_util_bps(const reserve_state& res) const {
   if (res.total_liquidity.amount <= 0) return 0;
   int128_t u = (int128_t)res.total_debt.amount * RATE_SCALE / res.total_liquidity.amount;
   if (u < 0) u = 0;
   if (u > (int128_t)RATE_SCALE) u = RATE_SCALE;
   return (uint64_t)u;
}

uint64_t tyche_market::_buffer_bps_by_util(uint64_t util_bps) const {
   // v2: util 越高 buffer 越厚
   if (util_bps < 7000) return 100;   // <70%  => 1%
   if (util_bps < 8500) return 200;   // 70-85 => 2%
   if (util_bps < 9500) return 500;   // 85-95 => 5%
   return 1000;                       // >=95% => 10%
}

int64_t tyche_market::_calc_target_borrow_rate(const reserve_state& res, uint64_t util_bps) const {
   check(res.u_opt > 0 && res.u_opt < RATE_SCALE, "u_opt must be in (0, 100%)");
   check(res.r0 <= res.r_opt && res.r_opt <= res.r_max, "invalid interest rate curve");

   if (util_bps <= res.u_opt) {
      int128_t slope = (int128_t)(res.r_opt - res.r0) * util_bps / res.u_opt;
      return res.r0 + (int64_t)slope;
   }

   uint64_t excess = util_bps > RATE_SCALE ? RATE_SCALE : util_bps;
   excess -= res.u_opt;
   int128_t slope = (int128_t)(res.r_max - res.r_opt) * excess / (RATE_SCALE - res.u_opt);
   return res.r_opt + (int64_t)slope;
}

int64_t tyche_market::_calc_borrow_rate(const reserve_state& res, uint64_t util_bps) const {
   int64_t target = _calc_target_borrow_rate(res, util_bps);

   // 首次/旧数据兼容：last_borrow_rate_bp = 0 时，直接用 target 或 r0
   int64_t last =
            (res.borrow_index.borrow_rate_bp == 0)
            ? (int64_t)res.r0
            : (int64_t)res.borrow_index.borrow_rate_bp;

   int64_t step = (int64_t)res.max_rate_step_bp;
   if (step <= 0) return target;

   int64_t delta = target - last;
   if (delta >  step) delta =  step;
   if (delta < -step) delta = -step;

   int64_t applied = last + delta;

   // guardrail：保持在[r0, r_max]
   if (applied < (int64_t)res.r0)   applied = (int64_t)res.r0;
   if (applied > (int64_t)res.r_max) applied = (int64_t)res.r_max;
   return applied;
}


asset tyche_market::_supply_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount) const {
   check(amount.symbol == total_amount.symbol, "symbol mismatch");
   if (total_amount.amount == 0 || total_shares.amount == 0) {
      return amount;
   }
   int128_t numerator   = (int128_t)amount.amount * total_shares.amount;
   int64_t  share_value = static_cast<int64_t>(numerator / total_amount.amount);
   check(share_value > 0, "supply amount too small");
   return asset(share_value, amount.symbol);
}

asset tyche_market::_borrow_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount) const {
   check(amount.symbol == total_amount.symbol, "symbol mismatch");
   if (total_amount.amount == 0 || total_shares.amount == 0) {
      check(amount.amount > 0, "borrow too small");
      return amount;
   }
   int128_t numerator   = (int128_t)amount.amount * total_shares.amount;
   int128_t denominator = total_amount.amount;
   int64_t  share_value = static_cast<int64_t>((numerator + denominator - 1) / denominator);
   check(share_value > 0, "borrow too small");
   return asset(share_value, amount.symbol);
}

asset tyche_market::_repay_shares_from_amount(const asset& amount,const asset& total_shares,const asset& total_amount) const {
   check(amount.symbol == total_amount.symbol, "symbol mismatch");
   check(total_amount.amount > 0 && total_shares.amount > 0, "repay too small");

   int128_t numerator   = (int128_t)amount.amount * total_shares.amount;
   int128_t denominator = total_amount.amount;

   int64_t share_value = static_cast<int64_t>( numerator / denominator);

   check(share_value > 0, "repay too small");
   return asset(share_value, amount.symbol);
}

asset tyche_market::_withdraw_shares_from_amount(const asset& amount,const asset& total_shares,const asset& total_amount) const {
   check(amount.symbol == total_amount.symbol, "symbol mismatch");

   // 不允许在异常池状态下 withdraw
   check(total_amount.amount > 0 && total_shares.amount > 0, "withdraw too small");

   int128_t numerator   = (int128_t)amount.amount * total_shares.amount;
   int128_t denominator = total_amount.amount;

   // withdraw：必须 ceil，多扣 shares，防止用户多提
   int64_t share_value = static_cast<int64_t>((numerator + denominator - 1) / denominator);

   check(share_value > 0, "withdraw too small");
   return asset(share_value, amount.symbol);
}

asset tyche_market::_amount_from_shares(const asset& shares, const asset& total_shares, const asset& total_amount) const {
   check(shares.symbol == total_shares.symbol, "symbol mismatch");
   if (total_shares.amount == 0 || total_amount.amount == 0) {
      return asset(0, total_amount.symbol);
   }
   int128_t numerator    = (int128_t)shares.amount * total_amount.amount;
   int64_t  asset_amount = static_cast<int64_t>(numerator / total_shares.amount);
   return asset(asset_amount, total_amount.symbol);
}

int64_t tyche_market::available_liquidity(const reserve_state& res) const {
    uint64_t util = _util_bps(res);
    uint64_t buffer_bp = _buffer_bps_by_util(util);

    int128_t buffer =
        (int128_t)res.total_liquidity.amount * buffer_bp / RATE_SCALE;

    int128_t avail = (int128_t)res.total_liquidity.amount  - buffer;

    if (avail <= 0) return 0;
    return (int64_t)avail;
}

tyche_market::valuation tyche_market::_compute_valuation(name owner)
{
    positions_t positions(get_self(), get_self().value);
    auto owner_idx = positions.get_index<"byowner"_n>();

    prices_t prices(get_self(), get_self().value);
    reserves_t reserves(get_self(), get_self().value);

    valuation result{};

    auto itr = owner_idx.lower_bound(owner.value);

    while (itr != owner_idx.end() && itr->owner == owner)
    {

        auto res_itr = reserves.find(itr->sym_code.raw());
        if (res_itr == reserves.end())
        {
            ++itr;
            continue;
        }

        // reserve_state res = _accrue(*res_itr);
        const reserve_state &res = *res_itr;

        // ---------- supply / debt amount ----------
        asset supply_amount = _amount_from_shares(itr->supply_shares, res.total_supply_shares, res.total_liquidity);
        asset debt_amount = _amount_from_shares(itr->borrow_shares, res.total_borrow_shares, res.total_debt);

        // ---------- 是否需要价格 ----------
        bool need_price = (debt_amount.amount > 0) || (itr->collateral && res.max_ltv > 0 && supply_amount.amount > 0);

        if (!need_price)
        {
            ++itr;
            continue;
        }

        // ---------- price ----------
        asset price = _get_fresh_price(prices, itr->sym_code);

        // ---------- collateral value ----------
        if (itr->collateral && supply_amount.amount > 0 && res.max_ltv > 0)
        {
            int128_t v = value_of(supply_amount, price);
            result.collateral_value += v * res.liquidation_threshold / RATE_SCALE;
            result.max_borrowable_value += v * res.max_ltv / RATE_SCALE;
        }

        // ---------- debt value ----------
        if (debt_amount.amount > 0)
        {
            int128_t v = value_of(debt_amount, price);
            result.debt_value += v;
        }

        ++itr;
    }

    return result;
}

asset tyche_market::_get_fresh_price(prices_t& prices, symbol_code sym) const {
    auto itr = prices.find(sym.raw());
    check(itr != prices.end(), "price not available");

    auto now = current_time_point();
    auto freshness = now - itr->updated_at;

    int64_t ttl_us = (int64_t)_gstate.price_ttl_sec * 1'000'000;

    // v3：emergency 下放宽价格有效期（防止 oracle 抖动）
    if (_gstate.emergency_mode) {
        ttl_us *= 2;   // 或 3，取决于你的风险偏好
    }

    check(freshness.count() <= ttl_us, "price stale");

    // asset：price.amount + price.symbol
    return itr->price;
}

void tyche_market::_check_price_available(symbol_code sym) const {
   prices_t prices(get_self(), get_self().value);
   _get_fresh_price(prices, sym);
}

position_row* tyche_market::_get_or_create_position(positions_t& table,name owner,symbol_code sym,const asset& base_symbol_amount) {
    auto idx = table.get_index<"ownerreserve"_n>();
    auto itr = idx.find(((uint128_t)owner.value << 64) | sym.raw());

    // 已存在，直接返回 canonical 行
    if (itr != idx.end()) {
        auto canonical = table.find(itr->id);
        return const_cast<position_row*>(&(*canonical));
    }

    // 新建 position（不隐式开启 collateral）
    auto pk = table.available_primary_key();
    table.emplace(get_self(), [&](auto& row) {
        row.id            = pk;
        row.owner         = owner;
        row.sym_code      = sym;
        row.supply_shares = asset(0, base_symbol_amount.symbol);
        row.borrow_shares = asset(0, base_symbol_amount.symbol);
        row.collateral    = false;
    });

    auto new_itr = table.find(pk);
    return const_cast<position_row*>(&(*new_itr));
}


void tyche_market::_transfer_out(name token_contract, name to, const asset& quantity, const string& memo) {
   eosio::action(
      permission_level{get_self(), "active"_n},
      token_contract,
      "transfer"_n,
      std::make_tuple(get_self(), to, quantity, memo)).send();
}


// 用户存款利息结算（指数差值 × 份额）
void tyche_market::_settle_supply_interest( position_row& pos, const reserve_state& res) {
    auto& pool = res.supply_index;
    auto& user = pos.supply_interest;

    // 首次结算：只记录锚点，不给利息
   if (user.index_id == 0) {
      user.last_reward_per_share = pool.reward_per_share;
      user.index_id = pool.index_id;
      return;
   }

    // delta_rps = pool_rps - user_rps
    int128_t delta_rps =
        (int128_t)pool.reward_per_share -
        (int128_t)user.last_reward_per_share;

    if (delta_rps > 0 && pos.supply_shares.amount > 0) {
        int128_t pending = delta_rps * pos.supply_shares.amount / HIGH_PRECISION;

        if (pending > 0) {
            check(user.pending_interest.amount <=std::numeric_limits<int64_t>::max() - (int64_t)pending,"interest overflow");
            user.pending_interest.amount += (int64_t)pending;
        }
    }

    // 推进用户锚点
    user.last_reward_per_share = pool.reward_per_share;
    user.index_id = pool.index_id;
}


} // namespace tychefi
