#pragma once

#include <eosio/action.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <string>

#include "tyche.market.db.hpp"

namespace tychefi {

using namespace eosio;
using std::string;

class [[eosio::contract("tyche.market")]] tyche_market : public contract {
public:
   using contract::contract;

   tyche_market(name receiver, name code, datastream<const char*> ds)
   : contract(receiver, code, ds),
     _global(get_self(), get_self().value) {
      _gstate = _global.exists() ? _global.get() : global_t{};
   }

   ~tyche_market() {
      _global.set(_gstate, get_self());
   }
   /// 初始化全局管理员（只允许合约自身 init）
   ACTION init(const name& admin);

   /// 全局暂停开关（admin）
   ACTION setpause(const bool& paused);

   /// 价格 TTL（秒），用于 freshness 校验（admin）
   ACTION setpricettl(const uint32_t& ttl_sec);

   /// 清算 close factor（bps），限制单次最多偿还债务比例（admin）
   ACTION setclosefac(const uint64_t& close_factor_bp);

   /// 设置某资产的 USDT 报价（admin），会做归一化与单次波动限制
   ACTION setprice(const symbol_code& sym, const asset& price);

   /// 紧急模式开关（admin）：允许更宽 TTL / 更高 bonus 等
   ACTION setemergency(const bool& enabled);

   /// 紧急配置（admin）
   ACTION setemcfg(uint64_t bonus_bp, uint64_t max_bonus_bp);

   /// 修改已存在 reserve 的风控参数（admin）
   ACTION setreserve(symbol_code sym,
                     uint64_t max_ltv,
                     uint64_t liq_threshold,
                     uint64_t liq_bonus,
                     uint64_t reserve_factor);

   /// 新增 reserve（admin）
   ACTION addreserve(const extended_symbol& asset_sym,
                     const uint64_t& max_ltv,
                     const uint64_t& liq_threshold,
                     const uint64_t& liq_bonus,
                     const uint64_t& reserve_factor,
                     const uint64_t& u_opt,
                     const uint64_t& r0,
                     const uint64_t& r_opt,
                     const uint64_t& r_max);

   /// 提现本金（可能触发自动 claim 部分利息）
   ACTION withdraw(name owner, asset quantity);

   /// 主动 claim 存款利息
   ACTION claimint(name owner, symbol_code sym);

   /// 开关抵押品标记（用户）
   ACTION setcollat(name owner, symbol_code sym, bool enabled);

   /// 借款（用户）
   ACTION borrow(name owner, asset quantity);

   // =====================================================
   // Notify Entry
   // =====================================================

   /**
    * 统一入口：on_notify transfer
    * - supply: memo="supply"
    * - repay : memo="repay:borrower"
    * - liquidate: memo="liquidate:borrower:DEBT:COLL"
    */
   [[eosio::on_notify("*::transfer")]]
   void on_transfer(const name& from,const name& to,const asset& quantity,const string& memo);




private:
   global_singleton _global;
   global_t _gstate;

   struct action_ctx {
    eosio::time_point_sec now;

    // 防重复推进
    std::set<uint64_t> accrued_symraw;

    // action 内缓存：保证 valuation/repay/liquidate 用同一份 res
    std::map<uint64_t, reserve_state> reserve_cache;
   };

   reserve_state& _get_reserve(action_ctx& ctx, reserves_t& reserves, symbol_code sym);

   /**
    * positions 建议 scope=owner（你偏好的设计）
    * - 这样按用户分表天然隔离，遍历 owner 全仓位更直观
    * - 如果你暂时未迁移，可先维持 self scope，在 cpp 内统一封装
    */
   positions_t _positions(name scope_owner) const { return positions_t(get_self(), scope_owner.value); }

   // =====================================================
   // Flow-level (动作编排：只在 action/notify 中调用)
   // =====================================================

   /// 用户存款（from transfer）
   void _on_supply(const name& owner, const asset& quantity);
   /// 还款（from transfer）
   void _on_repay(const name& payer,const name& borrower,const asset& quantity);

   /// 清算（from transfer）
   void _on_liquidate(const name& liquidator,
                     const name& borrower,
                     const symbol_code& debt_sym,
                     const asset& repay_amount,
                     const symbol_code& collateral_sym);

   // =====================================================
   // State-level (协议步骤：单职责、无隐式 now)
   // =====================================================

   /// 利率刷新：只根据当前状态更新 borrow_rate_bp（不推进时间）
   void _update_borrow_rate(reserve_state& res);

   /// 推进 borrow index（倍率模型）
   void _accrue_borrow_index(reserve_state& res, time_point_sec now);

   /// 推进 supply reward index（按 interest_realized-interest_claimed 进行均摊）
   void _accrue_supply_index(reserve_state& res, time_point_sec now);

   /// 结算用户存款利息：pool.reward_per_share - user.last_reward_per_share
   void _settle_supply_interest(position_row& pos, const reserve_state& res);

   /// 结算用户借款利息：基于 borrow_index 变化写入 pos.borrow.accrued_interest（不收钱）
   void _settle_borrow_interest(const reserve_state& res, position_row& pos);

   // =====================================================
   // Repay / Liquidate core (snapshot 内核：不直接改表)
   // =====================================================

   /**
    * 还款内核（快照上执行）：
    * - 先吃 accrued_interest（利息）
    * - 再还 scaled 本金
    * - 多余 refund（由 flow 层决定是否转回）
    *
    * 返回 repay_result（定义在 db.hpp）
    */
   repay_result _repay_by_snapshot(reserve_state& res,
                                    position_row&  pos,
                                    int64_t        pay_amount,
                                    bool           do_settle = true);

   /**
    * 清算内核（快照上执行）：
    * - repay_by_snapshot 执行偿还（close factor 限制可在此做）
    * - 计算 seize_amount，并扣 collateral shares
    * - 返回实际 seize
    */
   liquidate_result _liquidate_internal(reserve_state& debt_res,
                                          reserve_state& coll_res,
                                          position_row&  debt_pos,
                                          position_row&  coll_pos,
                                          int64_t   repay_amount,
                                          asset  debt_price,
                                          asset  coll_price);

   // =====================================================
   // Math-level (纯数学/换算：可单测，不读表)
   // =====================================================

   /// 利用率（bps）：debt / (liquidity + debt)
   uint64_t _util_bps(const reserve_state& res) const;

   /// buffer（bps）：按利用率动态加厚（仅用于“可借出现金”保护）
   uint64_t _buffer_bps_by_util(uint64_t util_bps) const;

   /// 目标利率曲线（bps）
   int64_t _calc_target_borrow_rate(const reserve_state& res, uint64_t util_bps) const;

   /// 平滑 + guardrail 后的实际利率（bps）
   int64_t _calc_borrow_rate(const reserve_state& res, uint64_t util_bps) const;

   /// 可用流动性（扣 buffer）
   int64_t _available_liquidity(const reserve_state& res) const;

   /// amount -> scaled（按 index 缩放）
   int128_t _scaled_from_amount(int64_t amount, uint128_t borrow_index) const;

   /// scaled -> amount（按 index 还原）
   int64_t  _amount_from_scaled(int128_t scaled, uint128_t borrow_index) const;

   // =====================================================
   // Shares conversion (供给侧份额换算)
   // =====================================================

   asset _supply_shares_from_amount(const asset& amount,
                                    const asset& total_shares,
                                    const asset& total_amount) const;

   asset _withdraw_shares_from_amount(const asset& amount,
                                    const asset& total_shares,
                                    const asset& total_amount) const;

   asset _amount_from_shares(const asset& shares,
                           const asset& total_shares,
                           const asset& total_amount) const;

   // =====================================================
   // Price / Valuation
   // =====================================================

   /// 读取并校验价格 freshness；返回 USDT 计价 price（symbol 必须 USDT_SYM）
   asset _get_fresh_price(prices_t& prices, symbol_code sym) const;

   /// 用于提前 fail-fast：借贷/抵押/清算前检查
   void _check_price_available(symbol_code sym) const;

   struct valuation {
      int128_t collateral_value     = 0;  // 已乘 liquidation_threshold 的抵押折算值（USDT最小单位）
      int128_t max_borrowable_value = 0;  // 已乘 max_ltv 的最大可借值（USDT最小单位）
      int128_t debt_value           = 0;  // 债务价值（USDT最小单位）
   };

   valuation _compute_valuation(
      action_ctx& ctx,
      name owner,
      const position_row* override_pos = nullptr,
      symbol_code override_sym = symbol_code());

   void _check_health_factor(
        action_ctx& ctx,
        name owner,
        const position_row* override_pos = nullptr,
        symbol_code override_sym = symbol_code());

   // =====================================================
   // Position helpers
   // =====================================================

   /**
    * 获取或创建 position
    * - 不隐式开启 collateral
    * - 新建时初始化 supply_shares=0 / borrow 状态锚点
    */
   position_row* _get_or_create_position(positions_t& table,const reserve_state& res,symbol_code sym) ;

   // =====================================================
   // Token transfer
   // =====================================================

   /// 合约向外转账（提现/借款/清算 seize/refund 等）
   void _transfer_out(name token_contract,
                     name to,
                     const asset& quantity,
                     const string& memo);


   static inline void _safe_add_i64(int64_t& dst, int64_t v, const char* err_msg) {
      check(v >= 0, err_msg);
      check(dst <= std::numeric_limits<int64_t>::max() - v, err_msg);
      dst += v;
   }

   static inline void _safe_sub_i64(int64_t& dst, int64_t v, const char* err_msg) {
      check(v >= 0, err_msg);
      check(dst >= v, err_msg);
      dst -= v;
   }
   static inline int128_t pow10_i128(uint8_t p) {
      int128_t r = 1;
      for (uint8_t i = 0; i < p; ++i) r *= 10;
      return r;
   }

   // value_of: token amount -> USDT min-unit
   static inline int128_t value_of(const asset& amount, const asset& price_usdt) {
      // price_usdt: USDT symbol
      // result: USDT min-unit
      eosio::check(price_usdt.amount > 0, "invalid price");
      int128_t num = (int128_t)amount.amount * (int128_t)price_usdt.amount;
      int128_t den = pow10_i128(amount.symbol.precision());
      return num / den;
   }

   // HF Simulation Abstraction
   void _simulate_position_change(action_ctx& ctx,name owner,reserves_t& reserves,positions_t& positions,symbol_code sym,const position_change& change);


   int64_t _user_real_debt_amt(const reserve_state& res,const position_row& pos) const ;
   int64_t _reserve_real_total_debt_amt(const reserve_state& res) const;
   void _flush_reserve(action_ctx& ctx, reserves_t& reserves, symbol_code sym);

};

} // namespace tychefi