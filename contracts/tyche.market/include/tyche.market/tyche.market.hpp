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
      _global(get_self(), get_self().value)
    {
        _gstate = _global.exists() ? _global.get() : global_t{};
    }

    ~tyche_market() {
        _global.set(_gstate, get_self());
    }

   ACTION init(name admin);
   ACTION setpause(bool paused);
   // 设置价格最大允许过期时间
   ACTION setpricettl(uint32_t ttl_sec);
   // 限制单次清算最大比例
   ACTION setclosefac(uint64_t close_factor_bp);
   // 更新链上价格
   ACTION setprice(symbol_code sym, uint64_t price);
   // 开启 / 关闭 emergency 模式
   ACTION setemergency(bool enabled);
   // 配置 emergency 下的激励参数
   ACTION setemcfg(uint64_t bonus_bp,uint64_t max_bonus_bp,uint64_t backstop_min);
   // 新增一个可借贷资产
   ACTION addreserve(const extended_symbol& asset_sym,
                                       const   uint64_t& max_ltv,
                                       const   uint64_t& liq_threshold,
                                       const   uint64_t& liq_bonus,
                                       const   uint64_t& reserve_factor,
                                       const   uint64_t& u_opt,
                                       const   uint64_t& r0,
                                       const   uint64_t& r_opt,
                                       const   uint64_t& r_max) ;

   // 提取存款
   ACTION withdraw(name owner, asset quantity);
   // 切换某资产是否作为抵押，关闭时做 HF 校验
   ACTION setcollat(name owner, symbol_code sym, bool enabled);
   // 借出资产
   ACTION borrow(name owner, asset quantity);

   [[eosio::on_notify("*::transfer")]]
    void on_transfer(const name& from,
                     const name& to,
                     const asset& quantity,
                     const std::string& memo);

private:

   global_singleton _global;
   global_t _gstate;
   // 用户存款，计算并发放 supply_shares
   void _on_supply(const name& owner,const asset& quantity);
   // 偿还借款，自动 clamp 不超过债务，更新 shares + reserve
   void _on_repay(const name& payer,const name& borrower,const asset& quantity);
   // 执行清算
   void _on_liquidate(name liquidator,name borrower,symbol_code debt_sym,asset repay_amount,symbol_code collateral_sym);

   reserve_state _require_reserve(const symbol& sym);
   // 根据 elapsed 时间计算利息
   reserve_state _accrue(reserve_state res);
   // 	_accrue + 写回
   reserve_state _accrue_and_store(reserves_t& reserves, reserves_t::const_iterator itr);
   // 计算利用率
   uint64_t _util_bps(const reserve_state& res) const;
   // 动态 buffer，高利用率 → 更厚 buffer -- 防挤兑、防清算连锁反应
   uint64_t _buffer_bps_by_util(uint64_t util_bps) const;

   // 曲线函数保持不变：计算“目标利率”
   int64_t _calc_target_borrow_rate(const reserve_state& res, uint64_t util_bps) const;
   // step cap 基于 last_borrow_rate_bp 限速， 防利率跳变
   int64_t  _calc_borrow_rate(const reserve_state& res, uint64_t util_bps) const;
   // 存款 → shares
   asset _supply_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount)const;
   // 借款 → shares（向上取整）
   asset _borrow_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount)const;
   // 还款 → shares
   asset _repay_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount)const;
   // 提现 → shares
   asset _withdraw_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount)const;
   // shares → asset
   asset _amount_from_shares(const asset& shares, const asset& total_shares, const asset& total_amount)const;

   struct valuation {
      int128_t collateral_value     = 0;
      int128_t max_borrowable_value = 0;
      int128_t debt_value           = 0;
   };
   // 可借 / 可提流动性计算，扣除：	protocol reserve、动态 buffer。 -- 系统级流动性保护
   int64_t available_liquidity(const reserve_state& res) {
      uint64_t util_bps = _util_bps(res);
      uint64_t buffer_bps = _buffer_bps_by_util(util_bps);

      int128_t buffer = (int128_t)res.total_liquidity.amount * buffer_bps / RATE_SCALE;
      int128_t avail  = (int128_t)res.total_liquidity.amount
                     - (int128_t)res.protocol_reserve.amount
                     - buffer;

      if (avail <= 0) return 0;
      return (int64_t)avail;
   }
   // 遍历用户所有仓位，计算： 抵押价值、借款价值、最大可借
   valuation _compute_valuation(name owner);
   // 校验价格存在，校验 TTL（emergency 可放宽）
   uint64_t _get_fresh_price(prices_t& prices, symbol_code sym)const;
   // 语义包装，所有用户行为前置校
   void     _check_price_available(symbol_code sym)const;
   // 定位或创建用户 × 资产仓位，保证仓位唯一
   position_row* _get_or_create_position(positions_t& table,name owner,symbol_code sym,const asset& base_symbol_amount);

   void _transfer_from(name token_contract, name from, const asset& quantity, const string& memo);

   void _transfer_out(name token_contract, name to, const asset& quantity, const string& memo);
};

} // namespace tychefi