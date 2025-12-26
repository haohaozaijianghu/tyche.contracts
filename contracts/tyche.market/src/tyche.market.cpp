#include <tyche.market/tyche.market.hpp>

#include <cmath>
#include <limits>
#include <tuple>
#include "flon.token.hpp"
#include "utils.hpp"

namespace tychefi {

static constexpr int128_t MAX_RATE_DELTA = static_cast<int128_t>(std::numeric_limits<int64_t>::max());

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

void tyche_market::setprice(symbol_code sym, uint64_t price) {
   require_auth(_gstate.admin);
   CHECKC(price > 0, err::NOT_POSITIVE, "price must be positive");

   prices_t prices(get_self(), get_self().value);
   auto     itr = prices.find(sym.raw());
   auto     now = current_time_point();

   if (itr == prices.end()) {

      // === 首次设置价格 ===
      prices.emplace(get_self(), [&](auto& row) {
         row.sym_code   = sym;
         row.price      = price;
         row.updated_at = now;
      });

   } else {

      // === 防止同块多次改价 ===
      check(itr->updated_at < now,"price already updated in this block");

      // === 限制单次价格变动幅度 ===
      uint64_t last_price = itr->price;

      int128_t diff = (price > last_price)
                        ? (int128_t)price - last_price
                        : (int128_t)last_price - price;

      check(diff * RATE_SCALE <= (int128_t)last_price * MAX_PRICE_CHANGE_BP,"price change exceeds max allowed range");

      prices.modify(itr, get_self(), [&](auto& row) {
         row.price      = price;
         row.updated_at = now;
      });
   }
}

void tyche_market::setemergency(bool enabled){
      require_auth(_gstate.admin);
      _gstate.emergency_mode = enabled;
}

void tyche_market::setemcfg(uint64_t bonus_bp, uint64_t max_bonus_bp, uint64_t backstop_min){
      require_auth(_gstate.admin);
      check(bonus_bp <= max_bonus_bp, "bonus exceeds max");
      _gstate.emergency_bonus_bp     = bonus_bp;
      _gstate.max_emergency_bonus_bp = max_bonus_bp;
      _gstate.backstop_min_reserve   = backstop_min;
}

void tyche_market::addreserve(const extended_symbol& asset_sym,
                                       const   uint64_t& max_ltv,
                                       const   uint64_t& liq_threshold,
                                       const   uint64_t& liq_bonus,
                                       const   uint64_t& reserve_factor,
                                       const   uint64_t& u_opt,
                                       const   uint64_t& r0,
                                       const   uint64_t& r_opt,
                                       const   uint64_t& r_max) {
   require_auth(_gstate.admin);
   check(!_gstate.paused, "market paused");

   auto sym = asset_sym.get_symbol();

   // === symbol sanity ===
   check(sym.is_valid(), "invalid symbol");
   check(sym.precision() <= 8, "token precision too large");

   // === risk params ===
   check(max_ltv <= RATE_SCALE, "max_ltv too large");
   check(liq_threshold <= RATE_SCALE, "liquidation threshold too large");
   check(liq_threshold >= max_ltv, "liquidation threshold < max ltv");

   check(liq_bonus >= RATE_SCALE && liq_bonus <= RATE_SCALE * 2,
         "liquidation bonus must be between 1x and 2x");

   check(reserve_factor <= RATE_SCALE / 2, "reserve factor too high");

   // === interest model ===
   check(u_opt > 0 && u_opt < RATE_SCALE, "u_opt must be in (0, 100%)");
   check(r0 <= r_opt && r_opt <= r_max, "invalid interest curve");

   // liquidation math invariant
   check(
      (int128_t)liq_threshold * liq_bonus < (int128_t)RATE_SCALE * RATE_SCALE,
      "invalid liquidation parameters"
   );

   reserves_t reserves(get_self(), get_self().value);
   check(reserves.find(sym.code().raw()) == reserves.end(), "reserve already exists");

   reserves.emplace(get_self(), [&](auto& row) {
      row.sym_code            = sym.code();
      row.token_contract      = asset_sym.get_contract();

      row.max_ltv             = max_ltv;
      row.liquidation_threshold = liq_threshold;
      row.liquidation_bonus   = liq_bonus;
      row.reserve_factor      = reserve_factor;

      row.u_opt               = u_opt;
      row.r0                  = r0;
      row.r_opt               = r_opt;
      row.r_max               = r_max;
      row.max_rate_step_bp    = 200;        // 默认值
      row.last_borrow_rate_bp = r0;         // 初始化为 r0（最合理）
      row.total_liquidity     = asset(0, sym);
      row.total_debt          = asset(0, sym);
      row.total_supply_shares = asset(0, sym);
      row.total_borrow_shares = asset(0, sym);
      row.protocol_reserve    = asset(0, sym);

      row.last_updated        = current_time_point();
      row.paused              = false;
   });
}

void tyche_market::_on_supply(const name& owner, const asset& quantity) {

   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   // 确保价格可用（风控一致性）
   _check_price_available(quantity.symbol.code());

   // 计息（池级）
   auto res = _accrue_and_store(reserves, res_itr);

   // 计算 supply shares
   asset shares = _supply_shares_from_amount(quantity,res.total_supply_shares,res.total_liquidity);

   // === 更新用户仓位 ===
   positions_t positions(get_self(), get_self().value);
   auto pos_ptr = _get_or_create_position(positions,owner,quantity.symbol.code(),quantity);

   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.supply_shares += shares;
      row.collateral     = true; // 默认开启为抵押物
   });

   // === 更新池子状态 ===
   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = res.total_liquidity + quantity;
      row.total_supply_shares = res.total_supply_shares + shares;
      row.total_debt          = res.total_debt;
      row.protocol_reserve    = res.protocol_reserve;
      row.last_updated        = res.last_updated;
   });
}

void tyche_market::withdraw(name owner, asset quantity) {
   require_auth(owner);
   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   // 价格必须可用（HF 计算一致性）
   _check_price_available(quantity.symbol.code());

   positions_t positions(get_self(), get_self().value);
   auto owner_idx = positions.get_index<"ownerreserve"_n>();
   auto pos_itr   = owner_idx.find(
      (uint128_t(owner.value) << 64) | quantity.symbol.code().raw()
   );
   check(pos_itr != owner_idx.end(), "no position found");
   check(pos_itr->supply_shares.amount > 0, "no supply shares");

   // === 池子计息 ===
   auto res = _accrue_and_store(reserves, res_itr);

   // === 用户最大可提金额 ===
   asset max_withdrawable =
      _amount_from_shares(
         pos_itr->supply_shares,
         res.total_supply_shares,
         res.total_liquidity
      );
   check(quantity <= max_withdrawable, "withdraw exceeds balance");

   // === 池子可用流动性 ===
   check(quantity.amount <= available_liquidity(res), "insufficient liquidity");

   // === 计算 shares 变化 ===
   asset share_delta =
      _withdraw_shares_from_amount(
         quantity,
         res.total_supply_shares,
         res.total_liquidity
      );
   check(share_delta.amount > 0, "withdraw amount too small");

   // === 健康因子校验（提现后）===
   valuation val = _compute_valuation(owner);
   check(
      val.debt_value == 0 || val.collateral_value >= val.debt_value,
      "health factor below 1 after withdraw"
   );

   // === 转账 ===
   _transfer_out(res.token_contract, owner, quantity, "withdraw");

   // === 更新用户仓位 ===
   owner_idx.modify(pos_itr, owner, [&](auto& row) {
      row.supply_shares -= share_delta;
      if (row.supply_shares.amount == 0) {
         row.collateral = false; // 状态自洽：空仓位不作为抵押
      }
   });

   // === 更新池子 ===
   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = res.total_liquidity - quantity;
      row.total_supply_shares = res.total_supply_shares - share_delta;
      row.total_debt          = res.total_debt;
      row.protocol_reserve    = res.protocol_reserve;
      row.last_updated        = res.last_updated;
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

   // 价格必须可用
   _check_price_available(sym);

   // 不允许对空仓位开启抵押
   check(
      !enabled || pos_itr->supply_shares.amount > 0,
      "cannot enable collateral with zero supply"
   );

   if (pos_itr->collateral == enabled) return;

   owner_idx.modify(pos_itr, owner, [&](auto& row) {
      row.collateral = enabled;
   });

   if (!enabled) {
      valuation val = _compute_valuation(owner);
      check(
         val.debt_value == 0 || val.collateral_value >= val.debt_value,
         "health factor below 1 after disabling collateral"
      );
   }
}

void tyche_market::borrow(name owner, asset quantity) {
   require_auth(owner);
   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");
   check(res_itr->max_ltv > 0, "borrowing disabled for this asset");

   // 价格必须可用
   _check_price_available(quantity.symbol.code());

   // 计息
   auto res = _accrue_and_store(reserves, res_itr);

   // 流动性校验
   check(quantity.amount <= available_liquidity(res), "insufficient liquidity");

   // 获取 / 创建仓位
   positions_t positions(get_self(), get_self().value);
   auto pos_ptr = _get_or_create_position(positions,owner,quantity.symbol.code(),quantity);

   // === 计算 borrow shares ===
   asset borrow_shares = _borrow_shares_from_amount(quantity,res.total_borrow_shares,res.total_debt);
   check(borrow_shares.amount > 0, "borrow amount too small");

   // === 模拟写入，重新计算 HF ===
   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.borrow_shares += borrow_shares;
   });

   valuation val_after = _compute_valuation(owner);

   check(val_after.debt_value <= val_after.max_borrowable_value,"exceeds max LTV");
   check(val_after.collateral_value >= val_after.debt_value,"health factor below 1 after borrow");

   // === rollback 模拟 ===
   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.borrow_shares -= borrow_shares;
   });

   // === 真正转账 ===
   _transfer_out(res.token_contract, owner, quantity, "borrow");

   // === 真正落库 ===
   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.borrow_shares += borrow_shares;
   });

   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_debt          = res.total_debt + quantity;
      row.total_borrow_shares = res.total_borrow_shares + borrow_shares;
      row.total_liquidity     = res.total_liquidity;
      row.total_supply_shares = res.total_supply_shares;
      row.protocol_reserve    = res.protocol_reserve;
      row.last_updated        = res.last_updated;
   });
}

void tyche_market::_on_repay(const name& payer,const name& borrower,const asset& quantity) {
   // ========= 基础校验 =========
   check(quantity.amount > 0, "repay amount must be positive");
   check(is_account(borrower), "invalid borrower");

   // ========= reserve 校验 =========
   reserves_t reserves(get_self(), get_self().value);
   auto res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   // token 必须来自该 reserve 的 token_contract
   check(
      res_itr->token_contract == get_first_receiver(),
      "invalid token contract"
   );

   // ========= borrower 仓位 =========
   positions_t positions(get_self(), get_self().value);
   auto owner_idx = positions.get_index<"ownerreserve"_n>();
   auto pos_itr = owner_idx.find(
      (uint128_t(borrower.value) << 64) | quantity.symbol.code().raw()
   );
   check(pos_itr != owner_idx.end(), "no borrow position");
   check(pos_itr->borrow_shares.amount > 0, "no debt to repay");

   // ========= 池子计息（关键）=========
   auto res = _accrue_and_store(reserves, res_itr);

   // ========= 当前债务 =========
   asset current_debt = _amount_from_shares(
      pos_itr->borrow_shares,
      res.total_borrow_shares,
      res.total_debt
   );
   check(current_debt.amount > 0, "no outstanding debt");

   // ========= clamp repay =========
   asset repay_amount = quantity;
   if (repay_amount > current_debt) {
      repay_amount = current_debt;
   }

   // ========= 计算要减少的 borrow shares =========
   asset share_delta = _repay_shares_from_amount(
      repay_amount,
      res.total_borrow_shares,
      res.total_debt
   );
   check(share_delta.amount > 0, "repay amount too small");

   // ========= 更新 borrower 仓位 =========
   owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
      row.borrow_shares -= share_delta;
   });

   // ========= 更新 reserve =========
   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_debt          = res.total_debt - repay_amount;
      row.total_borrow_shares = res.total_borrow_shares - share_delta;
      row.total_liquidity     = res.total_liquidity + repay_amount;
      row.total_supply_shares = res.total_supply_shares;
      row.protocol_reserve    = res.protocol_reserve;
      row.last_updated        = res.last_updated;
   });
}

void tyche_market::_on_liquidate(name liquidator,name borrower,symbol_code debt_sym,asset repay_amount,symbol_code collateral_sym) {
   require_auth(liquidator);
   check(!_gstate.paused, "market paused");
   check(liquidator != borrower, "self liquidation not allowed");
   check(repay_amount.amount > 0, "repay amount must be positive");
   check(debt_sym != collateral_sym, "invalid liquidation asset");

   // ---------- 1) 先拿表 + 基础校验 ----------
   reserves_t reserves(get_self(), get_self().value);

   auto debt_itr   = reserves.find(debt_sym.raw());
   check(debt_itr != reserves.end(), "debt reserve not found");

   auto collat_itr = reserves.find(collateral_sym.raw());
   check(collat_itr != reserves.end(), "collateral reserve not found");

   check(!debt_itr->paused,   "debt reserve paused");
   check(!collat_itr->paused, "collateral reserve paused");

   // ---------- 2) accrue_and_store 让“HF判定”和“实际清算”用同一时点 ----------
   auto debt_res   = _accrue_and_store(reserves, debt_itr);
   auto coll_res   = _accrue_and_store(reserves, collat_itr);

   // ---------- 3) 查 borrower 仓位 ----------
   positions_t positions(get_self(), get_self().value);
   auto owner_idx = positions.get_index<"ownerreserve"_n>();

   auto debt_pos  = owner_idx.find(((uint128_t)borrower.value << 64) | debt_sym.raw());
   auto coll_pos  = owner_idx.find(((uint128_t)borrower.value << 64) | collateral_sym.raw());
   check(debt_pos != owner_idx.end(), "borrow position not found");
   check(coll_pos != owner_idx.end(), "collateral position not found");

   check(debt_pos->borrow_shares.amount > 0, "no outstanding debt");
   check(coll_pos->supply_shares.amount > 0, "no collateral supplied");
   check(coll_pos->collateral, "asset not enabled as collateral");

   // ---------- 4) 用已写回的储备状态重新估值，确保 eligibility 一致 ----------
   // 注意：_compute_valuation 内部会 _accrue(reserve) 但不写回；这里已经写回了关键两个 reserve，
   // 其余 reserve 若你要严格一致，可额外做“对 borrower 所有仓位的 reserve accrue_and_store”。
   valuation val = _compute_valuation(borrower);
   check(val.debt_value > 0, "no debt");
   check(val.collateral_value < val.debt_value, "position not eligible for liquidation");

   // ---------- 5) 当前债务 amount ----------
   asset borrower_debt = _amount_from_shares(debt_pos->borrow_shares,
                                             debt_res.total_borrow_shares,
                                             debt_res.total_debt);
   check(borrower_debt.amount > 0, "no outstanding debt");

   if (repay_amount > borrower_debt) repay_amount = borrower_debt;

   // ---------- 6) 价格读取 + close factor 上限 ----------
   prices_t prices(get_self(), get_self().value);
   uint64_t debt_price = _get_fresh_price(prices, debt_sym);
   uint64_t col_price  = _get_fresh_price(prices, collateral_sym);

   // debt_value / repay_value 用“价值尺度(PRICE_SCALE)”统一
   int128_t debt_value      = (int128_t)borrower_debt.amount * debt_price / PRICE_SCALE;
   int128_t repay_value     = (int128_t)repay_amount.amount  * debt_price / PRICE_SCALE;

   // close-factor cap
   int128_t max_repay_close = debt_value * _gstate.close_factor_bp / RATE_SCALE;

   // ---------- 7) HF 回到 1 的 cap（考虑 seized collateral 也会下降） ----------
   int128_t shortfall = val.debt_value - val.collateral_value; // >0
   // denom = RATE^2 - liq_threshold*liq_bonus
   int128_t denom = (int128_t)RATE_SCALE * RATE_SCALE -
                    (int128_t)coll_res.liquidation_threshold * coll_res.liquidation_bonus;

   int128_t max_repay_to_one = max_repay_close;
   if (denom > 0) {
      // ceil( shortfall * RATE^2 / denom )
      max_repay_to_one =
         (shortfall * (int128_t)RATE_SCALE * RATE_SCALE + denom - 1) / denom;
   }

   // 取三者 min：用户请求 repay_value、close-factor cap、to-one cap
   int128_t repay_value_cap = std::min<int128_t>(repay_value,
                              std::min(max_repay_close, max_repay_to_one));
   check(repay_value_cap > 0, "repay amount too small");

   // ---------- 8) 将 cap value 反推 cap amount（向上取整防欠还） ----------
   // capped_amount = ceil(repay_value_cap * PRICE_SCALE / debt_price)
   int64_t capped_amount = static_cast<int64_t>(
      (repay_value_cap * PRICE_SCALE + (int128_t)debt_price - 1) / (int128_t)debt_price
   );
   check(capped_amount > 0, "repay amount too small");

   repay_amount = asset(capped_amount, repay_amount.symbol);

   // cap 后仍不能超过真实债务
   if (repay_amount > borrower_debt) repay_amount = borrower_debt;
   check(repay_amount.amount > 0, "repay amount too small");

   // 更新 repay_value（以 cap 后 repay_amount 为准）
   repay_value = (int128_t)repay_amount.amount * debt_price / PRICE_SCALE;
   check(repay_value > 0, "repay amount too small");

   // ---------- 9) 计算要减少的 debt shares（必须 >0，否则 dust DoS） ----------
   asset debt_share_delta = _repay_shares_from_amount(repay_amount,
                                                      debt_res.total_borrow_shares,
                                                      debt_res.total_debt);
   check(debt_share_delta.amount > 0, "repay too small");

   // ---------- 10) 计算要 seize 的 collateral（含 bonus） ----------
   uint64_t bonus_bp = coll_res.liquidation_bonus;

   if (_gstate.emergency_mode) {
      uint64_t emergency_bonus = _gstate.emergency_bonus_bp;
      bonus_bp += emergency_bonus;

      uint64_t max_bonus = RATE_SCALE + _gstate.max_emergency_bonus_bp;
      bonus_bp = std::min(bonus_bp, max_bonus);

      check(
         debt_res.protocol_reserve.amount >= (int64_t)_gstate.backstop_min_reserve,
         "protocol backstop reserve too low"
      );
   }

   int128_t seize_value = repay_value * (int128_t)bonus_bp / RATE_SCALE;
   check(seize_value > 0, "seize value zero");

   // seize_amount = floor(seize_value * PRICE_SCALE / col_price)
   // （这里保持你原本 floor；若你希望“更严格不欠给清算人”，可以改成 ceil）
   int128_t seize_amt_128 = (seize_value * PRICE_SCALE) / (int128_t)col_price;
   check(seize_amt_128 > 0, "seize amount zero");
   check(seize_amt_128 <= (int128_t)std::numeric_limits<int64_t>::max(), "seize overflow");

   asset seize_asset = asset((int64_t)seize_amt_128, coll_res.total_liquidity.symbol);

   // ---------- 11) borrower collateral 是否足够（amount + share 两层校验） ----------
   asset collateral_balance = _amount_from_shares(coll_pos->supply_shares,
                                                  coll_res.total_supply_shares,
                                                  coll_res.total_liquidity);
   check(collateral_balance.amount >= seize_asset.amount, "insufficient collateral");

   asset coll_share_delta = _withdraw_shares_from_amount(seize_asset,
                                                         coll_res.total_supply_shares,
                                                         coll_res.total_liquidity);
   check(coll_share_delta.amount > 0, "seize too small");
   check(coll_share_delta.amount <= coll_pos->supply_shares.amount, "seize exceeds collateral shares");

   // ---------- 12) 执行转账 ----------
   _transfer_out(coll_res.token_contract, liquidator, seize_asset, "liquidate seize");

   // ---------- 13) 写仓位 ----------
   owner_idx.modify(debt_pos, same_payer, [&](auto& row) {
      row.borrow_shares -= debt_share_delta;
   });

   owner_idx.modify(coll_pos, same_payer, [&](auto& row) {
      row.supply_shares -= coll_share_delta;
   });

   // ---------- 14) 写 reserve（注意使用 debt_res/coll_res 的“已计息版本”做基准） ----------
   // debt reserve: debt 减少、liquidity 增加
   reserves.modify(debt_itr, same_payer, [&](auto& row) {
      row.total_debt          = debt_res.total_debt - repay_amount;
      row.total_borrow_shares = debt_res.total_borrow_shares - debt_share_delta;

      row.total_liquidity     = debt_res.total_liquidity + repay_amount;
      row.total_supply_shares = debt_res.total_supply_shares;

      row.protocol_reserve    = debt_res.protocol_reserve;
      row.last_updated        = debt_res.last_updated;
   });

   // collateral reserve: liquidity 减少、supply_shares 减少
   reserves.modify(collat_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = coll_res.total_liquidity - seize_asset;
      row.total_supply_shares = coll_res.total_supply_shares - coll_share_delta;

      row.total_debt          = coll_res.total_debt;
      row.total_borrow_shares = coll_res.total_borrow_shares;

      row.protocol_reserve    = coll_res.protocol_reserve;
      row.last_updated        = coll_res.last_updated;
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

reserve_state tyche_market::_accrue(reserve_state res) {
   auto now = current_time_point();
   if (res.last_updated >= now) {
      return res;
   }

   uint32_t elapsed = (uint32_t)(
      now.sec_since_epoch() - res.last_updated.sec_since_epoch()
   );

   if (elapsed == 0) {
      return res;
   }

   // ===== 无债务：利率锚回 r0（v2 关键）=====
   if (res.total_debt.amount <= 0) {
      res.last_borrow_rate_bp = res.r0;
      res.last_updated = now;
      return res;
   }

   uint64_t util_bps = res.total_liquidity.amount == 0
      ? 0
      : (uint64_t)((int128_t)res.total_debt.amount * RATE_SCALE
                   / res.total_liquidity.amount);

   int64_t borrow_rate_bps = _calc_borrow_rate(res, util_bps);

   check(borrow_rate_bps >= 0, "borrow rate underflow");
   check(borrow_rate_bps <= (int64_t)res.r_max, "borrow rate overflow");

   // v2：写回锚点
   res.last_borrow_rate_bp = (uint64_t)borrow_rate_bps;

   int128_t interest =
      (int128_t)res.total_debt.amount
      * borrow_rate_bps
      * elapsed
      / (RATE_SCALE * SECONDS_PER_YEAR);

   check(interest >= 0, "interest underflow");
   interest = std::min<int128_t>(interest, MAX_RATE_DELTA);

   if (interest > 0) {
      int128_t supply_income =
         interest * (RATE_SCALE - res.reserve_factor) / RATE_SCALE;
      int128_t protocol_income = interest - supply_income;

      res.total_debt.amount       += (int64_t)interest;
      res.total_liquidity.amount  += (int64_t)supply_income;
      res.protocol_reserve.amount += (int64_t)protocol_income;
   }

   res.last_updated = now;
   return res;
}

reserve_state tyche_market::_accrue_and_store(reserves_t& reserves, reserves_t::const_iterator itr) {
    auto res = _accrue(*itr);
    reserves.modify(itr, same_payer, [&](auto& row) {
        row = res;
    });
    return res;
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
   int64_t last = (res.last_borrow_rate_bp == 0) ? (int64_t)res.r0 : (int64_t)res.last_borrow_rate_bp;

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

asset tyche_market::_repay_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount) const {
   check(amount.symbol == total_amount.symbol, "symbol mismatch");
   if (total_amount.amount == 0 || total_shares.amount == 0) {
      check(false, "repay too small");
   }
   int128_t numerator   = (int128_t)amount.amount * total_shares.amount;
   int64_t  share_value = static_cast<int64_t>(numerator / total_amount.amount);
   check(share_value > 0, "repay too small");
   return asset(share_value, amount.symbol);
}

asset tyche_market::_withdraw_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount) const {
   check(amount.symbol == total_amount.symbol, "symbol mismatch");
   if (total_amount.amount == 0 || total_shares.amount == 0) {
      return amount;
   }
   int128_t numerator   = (int128_t)amount.amount * total_shares.amount;
   int128_t denominator = total_amount.amount;
   int64_t  share_value = static_cast<int64_t>((numerator + denominator - 1) / denominator);
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

tyche_market::valuation tyche_market::_compute_valuation(name owner) {
   positions_t positions(get_self(), get_self().value);
   auto        owner_idx = positions.get_index<"byowner"_n>();
   auto        itr       = owner_idx.find(owner.value);

   prices_t   prices(get_self(), get_self().value);
   reserves_t reserves(get_self(), get_self().value);

   valuation result;
   while (itr != owner_idx.end() && itr->owner == owner) {
      auto res_itr = reserves.find(itr->sym_code.raw());
      if (res_itr == reserves.end()) {
         ++itr;
         continue;
      }

      reserve_state res = _accrue(*res_itr);

      uint64_t price = _get_fresh_price(prices, itr->sym_code);

      asset supply_amount = _amount_from_shares(itr->supply_shares, res.total_supply_shares, res.total_liquidity);
      asset debt_amount   = _amount_from_shares(itr->borrow_shares, res.total_borrow_shares, res.total_debt);

      if (itr->collateral && supply_amount.amount > 0) {
         int128_t value = (int128_t)supply_amount.amount * price / PRICE_SCALE;
         result.collateral_value += value * res.liquidation_threshold / RATE_SCALE;
         result.max_borrowable_value += value * res.max_ltv / RATE_SCALE;
      }

      if (debt_amount.amount > 0) {
         int128_t value = (int128_t)debt_amount.amount * price / PRICE_SCALE;
         result.debt_value += value;
      }

      ++itr;
   }
   return result;
}

uint64_t tyche_market::_get_fresh_price(prices_t& prices, symbol_code sym) const {
   auto itr = prices.find(sym.raw());
   check(itr != prices.end(), "price not available");

   auto freshness = current_time_point() - itr->updated_at;
   int64_t ttl_us = (int64_t)_gstate.price_ttl_sec * 1'000'000;
   if (_gstate.emergency_mode) {
      ttl_us *= 2; // 或 3
   }
   check(freshness.count() <= ttl_us, "price stale");

   return itr->price;
}

void tyche_market::_check_price_available(symbol_code sym) const {
   prices_t prices(get_self(), get_self().value);
   _get_fresh_price(prices, sym);
}

position_row* tyche_market::_get_or_create_position(positions_t& table,
                                                   name owner,
                                                   symbol_code sym,
                                                   const asset& base_symbol_amount) {
   auto idx = table.get_index<"ownerreserve"_n>();
   auto itr = idx.find(((uint128_t)owner.value << 64) | sym.raw());
   if (itr != idx.end()) {
     auto canonical = table.find(itr->id);
     return const_cast<position_row*>(&(*canonical));
   }

   auto pk = table.available_primary_key();
   table.emplace(owner, [&](auto& row) {
      row.id             = pk;
      row.owner          = owner;
      row.sym_code       = sym;
      row.supply_shares  = asset(0, base_symbol_amount.symbol);
      row.borrow_shares  = asset(0, base_symbol_amount.symbol);
      row.collateral     = true;
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

} // namespace tychefi
