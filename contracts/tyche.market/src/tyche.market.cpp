#include <tyche.market/tyche.market.hpp>

#include <cmath>
#include <limits>
#include <tuple>
#include "flon.token.hpp"
#include "utils.hpp"

namespace tychefi {

using namespace eosio;
using std::string;

void tyche_market::init(const name& admin) {
    require_auth(get_self());
    CHECKC(!_global.exists(), err::RECORD_EXISTING, "already initialized");
    _gstate.admin = admin;
}

void tyche_market::setpause(const bool& paused) {
    require_auth(_gstate.admin);
    _gstate.paused = paused;
}

void tyche_market::setpricettl(const uint32_t& ttl_sec) {
    require_auth(_gstate.admin);
    _gstate.price_ttl_sec = ttl_sec;
}

void tyche_market::setclosefac(const uint64_t& close_factor_bp) {
    require_auth(_gstate.admin);
    CHECKC(close_factor_bp <= RATE_SCALE, err::PARAM_ERROR, "invalid close factor");
    _gstate.close_factor_bp = close_factor_bp;
}

void tyche_market::setemergency(const bool& enabled){
      require_auth(_gstate.admin);
      _gstate.emergency_mode = enabled;
}

void tyche_market::setemcfg(uint64_t bonus_bp, uint64_t max_bonus_bp) {
      require_auth(_gstate.admin);

      check(max_bonus_bp <= RATE_SCALE, "max_bonus_bp must be <= 10000");
      check(bonus_bp     <= max_bonus_bp, "bonus exceeds max");

      _gstate.emergency_bonus_bp     = bonus_bp;
      _gstate.max_emergency_bonus_bp = max_bonus_bp;
}


void tyche_market::setprice(const symbol_code& sym,const asset& price) {
    require_auth(_gstate.admin);
    CHECKC(price.symbol == USDT_SYM, err::PARAM_ERROR, "price must be USDT");

    prices_t prices = prices_t(get_self(), get_self().value);
    auto itr = prices.find(sym.raw());

    if (itr == prices.end()) {
        prices.emplace(get_self(), [&](auto& r){
            r.sym_code = sym;
            r.price = price;
            r.updated_at = current_time_point();
        });
    } else {
        prices.modify(itr, same_payer, [&](auto& r){
            r.price = price;
            r.updated_at = current_time_point();
        });
    }
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

    reserves_t reserves =  reserves_t(get_self(), get_self().value);
    auto pk = asset_sym.get_symbol().code().raw();
    CHECKC(reserves.find(pk) == reserves.end(), err::RECORD_EXISTING, "reserve exists");

    reserves.emplace(get_self(), [&](auto& r){
        r.sym_code              = asset_sym.get_symbol().code();
        r.token_contract        = asset_sym.get_contract();
        r.total_liquidity       = asset(0, asset_sym.get_symbol());
        r.total_debt            = asset(0, asset_sym.get_symbol());
        r.total_supply_shares   = asset(0, asset_sym.get_symbol());
        r.interest_realized     = asset(0, asset_sym.get_symbol());
        r.interest_claimed      = asset(0, asset_sym.get_symbol());

        r.max_ltv = max_ltv;
        r.liquidation_threshold = liq_threshold;
        r.liquidation_bonus = liq_bonus;
        r.reserve_factor = reserve_factor;

        r.u_opt = u_opt;
        r.r0 = r0;
        r.r_opt = r_opt;
        r.r_max = r_max;
        r.max_rate_step_bp = 200;

        r.borrow_index.index = HIGH_PRECISION;
        r.borrow_index.borrow_rate_bp = r0;
        r.borrow_index.last_updated = current_time_point();
    });
}

void tyche_market::setreserve(symbol_code sym, uint64_t max_ltv,uint64_t liq_threshold,uint64_t liq_bonus, uint64_t reserve_factor) {
    require_auth(_gstate.admin);
    check(!_gstate.paused, "market paused");

    // ========= 基础校验 =========
    check(max_ltv <= RATE_SCALE, "max_ltv overflow");
    check(liq_threshold <= RATE_SCALE, "liq_threshold overflow");
    check(liq_bonus >= RATE_SCALE, "liq_bonus must be >= 100%");
    check(reserve_factor <= RATE_SCALE, "reserve_factor overflow");

    // 关系约束（核心风控不变量）
    // max_ltv <= liquidation_threshold <= 100%
    check(max_ltv <= liq_threshold, "max_ltv must be <= liquidation_threshold");

    reserves_t reserves(get_self(), get_self().value);
    auto itr = reserves.find(sym.raw());
    check(itr != reserves.end(), "reserve not found");

    // ========= 不允许在 paused reserve 上修改 =========
    check(!itr->paused, "reserve paused");

    // ========= 写入（只改参数） =========
    reserves.modify(itr, same_payer, [&](auto& row) {
        row.max_ltv               = max_ltv;
        row.liquidation_threshold = liq_threshold;
        row.liquidation_bonus     = liq_bonus;
        row.reserve_factor        = reserve_factor;
    });
}

void tyche_market::borrow(name owner, asset quantity) {
    require_auth(owner);
    check(!_gstate.paused, "market paused");
    check(quantity.amount > 0, "borrow must be positive");

    action_ctx ctx{ .now = current_time_point() };
    const symbol_code sym = quantity.symbol.code();
    _check_price_available(sym);

    reserves_t  reserves(get_self(), get_self().value);
    positions_t positions(get_self(), owner.value);

    // ① 推进 reserve（历史利息结算）
    reserve_state& res = _get_reserve(ctx, reserves, sym);

    // ② HF 模拟
    position_change ch{};
    ch.borrow_scaled_delta = _scaled_from_amount(quantity.amount, res.borrow_index.index);
    check(ch.borrow_scaled_delta > 0, "borrow too small");
    _simulate_position_change(ctx, owner, reserves, positions, sym, ch);

    // ③ 流动性检查
    check(quantity.amount <= _available_liquidity(res), "insufficient liquidity");

    // ④ load / create position
    auto pos_itr = positions.find(sym.raw());
    position_row pos =
        (pos_itr == positions.end())
        ? *(_get_or_create_position(positions, res, sym))
        : *pos_itr;

    // ⑤ 结息 or 建锚点（唯一位置）
    if (pos.borrow.borrow_scaled > 0) {
        _settle_borrow_interest(res, pos);
    } else {
        pos.borrow.last_borrow_index = res.borrow_index.index;
        pos.borrow.id                = res.borrow_index.id;
    }

    // ⑥ 借款入账
    int128_t scaled_add = _scaled_from_amount(quantity.amount, res.borrow_index.index);
    pos.borrow.borrow_scaled += scaled_add;
    pos.borrow.last_updated   = ctx.now;

    res.total_borrow_scaled += scaled_add;
    res.total_liquidity     -= quantity;

    // ⑦ 更新利率（用于下一时间段）
    _update_borrow_rate(res);

    // ⑧ commit
    positions.modify(positions.find(sym.raw()), same_payer, [&](auto& r){
        r = pos;
    });
    _flush_reserve(ctx, reserves, sym);

    _transfer_out(res.token_contract, owner, quantity, "borrow");
}

void tyche_market::withdraw(name owner, asset quantity) {
    require_auth(owner);
    check(!_gstate.paused, "market paused");
    check(quantity.amount > 0, "quantity must be positive");

    action_ctx ctx{ .now = current_time_point() };
    const symbol_code sym = quantity.symbol.code();
    reserves_t  reserves(get_self(), get_self().value);
    positions_t positions(get_self(), owner.value);

    auto pos_itr = positions.find(sym.raw());
    check(pos_itr != positions.end(), "no position");
    check(pos_itr->supply_shares.amount > 0, "no supply");

    reserve_state& res = _get_reserve(ctx, reserves, sym);
    position_row   pos = *pos_itr;

    // settle supply interest（withdraw 的余额计算依赖）
    _settle_supply_interest(pos, res);
    asset max_withdrawable = _amount_from_shares(pos.supply_shares,res.total_supply_shares,res.total_liquidity);
    check(quantity <= max_withdrawable, "withdraw exceeds balance");

    int64_t avail = _available_liquidity(res);
    check(quantity.amount <= avail, "insufficient liquidity");
    asset share_delta = _withdraw_shares_from_amount(quantity,res.total_supply_shares,res.total_liquidity);

    // 额外：份额赎回时等价领取对应比例的已分配利息，防止重复 claim
    int64_t original_shares = pos.supply_shares.amount;
    if (original_shares > 0 && pos.supply_interest.pending_interest.amount > 0 && share_delta.amount > 0) {
        int128_t interest_i128 = (int128_t)pos.supply_interest.pending_interest.amount * share_delta.amount / original_shares;
        int64_t interest_paid  = (int64_t)interest_i128;
        if (interest_paid > 0) {
            check(res.interest_claimed.amount <= std::numeric_limits<int64_t>::max() - interest_paid, "interest_claimed overflow");
            check(res.interest_claimed.amount + interest_paid <= res.interest_realized.amount, "interest exceeds realized");
            check(res.supply_index.indexed_available >= (uint64_t)interest_paid, "indexed_available underflow");

            pos.supply_interest.pending_interest.amount -= interest_paid;
            pos.supply_interest.claimed_interest.amount += interest_paid;
            res.interest_claimed.amount += interest_paid;
            res.supply_index.indexed_available -= (uint64_t)interest_paid;
        }
    }

    // ① HF 模拟（语义唯一）
    if (pos.collateral) {
        position_change ch{};
        ch.supply_shares_delta = -share_delta.amount;
        _simulate_position_change(ctx, owner, reserves, positions, sym, ch);
    }

    // ② Mutate
    pos.supply_shares -= share_delta;
    if (pos.supply_shares.amount == 0) pos.collateral = false;

    res.total_supply_shares -= share_delta;
    res.total_liquidity     -= quantity;

    // ③ Commit
    positions.modify(pos_itr, same_payer, [&](auto& r){ r = pos; });

     _flush_reserve(ctx, reserves, sym);
    _transfer_out(res.token_contract, owner, quantity, "withdraw");

}
// 在当前 reserve 状态下，计算“在不破坏系统流动性安全的前提下，最多还能被拿走的现金量”
int64_t tyche_market::_available_liquidity(const reserve_state& res) const {
    uint64_t util = _util_bps(res);
    uint64_t buffer_bp = _buffer_bps_by_util(util);

    int128_t buffer = (int128_t)res.total_liquidity.amount * (int128_t)buffer_bp / (int128_t)RATE_SCALE;
    int128_t avail  = (int128_t)res.total_liquidity.amount - buffer;
    return (avail > 0) ? (int64_t)avail : 0;
}
// 将“已经记账但尚未提走的供应利息”，从池子里安全地转给用户
void tyche_market::claimint(name owner, symbol_code sym) {
    require_auth(owner);
    check(!_gstate.paused, "market paused");

    action_ctx ctx{ .now = current_time_point() };
    reserves_t  reserves(get_self(), get_self().value);
    positions_t positions(get_self(), owner.value);

    auto pos_itr = positions.find(sym.raw());
    check(pos_itr != positions.end(), "no position");

    auto& res = _get_reserve(ctx, reserves, sym);
    position_row pos = *pos_itr;

    // ① settle supply interest（只推进用户）
    _settle_supply_interest(pos, res);
    int64_t pending = pos.supply_interest.pending_interest.amount;
    check(pending > 0, "no interest");

    asset available = res.interest_realized - res.interest_claimed;
    check(available.amount > 0, "no distributable interest");
    int64_t claim_amt = std::min(pending, available.amount);

    check(claim_amt > 0, "claim zero");
    asset claim_asset(claim_amt, res.total_liquidity.symbol);
    // ② commit position
    positions.modify(pos_itr, same_payer, [&](auto& r){
        r = pos;
        r.supply_interest.pending_interest.amount -= claim_amt;
        r.supply_interest.claimed_interest        += claim_asset;
    });

    // ③ commit reserve
    auto res_itr = reserves.find(sym.raw());
    reserves.modify(res_itr, same_payer, [&](auto& r){
        r.interest_claimed += claim_asset;
        check(r.supply_index.indexed_available >= (uint64_t)claim_amt, "indexed_available underflow");
        r.supply_index.indexed_available -= (uint64_t)claim_amt;
    });

    _transfer_out(res.token_contract, owner, claim_asset, "claim interest");
}
// 切换某个仓位是否作为抵押品（collateral），且必须保证切换后 Health Factor 仍然 ≥ 1
void tyche_market::setcollat(name owner, symbol_code sym, bool enabled) {
    require_auth(owner);
    check(!_gstate.paused, "market paused");

    action_ctx ctx{ .now = current_time_point() };
    reserves_t  reserves(get_self(), get_self().value);
    positions_t positions(get_self(), owner.value);

    auto pos_itr = positions.find(sym.raw());
    check(pos_itr != positions.end(), "no position");
    reserve_state& res = _get_reserve(ctx, reserves, sym);

    if (enabled) {
        check(pos_itr->supply_shares.amount > 0, "no supply");
        check(res.max_ltv > 0, "asset not collateralizable");
        _check_price_available(sym);
    }

    if (pos_itr->collateral == enabled) return;

    // ① HF 模拟（语义唯一）
    position_change ch{};
    ch.collateral_override     = enabled;
    _simulate_position_change(ctx, owner, reserves, positions, sym, ch);

    // ② Commit
    positions.modify(pos_itr, same_payer, [&](auto& r){
        r.collateral = enabled;
    });
}

void tyche_market::on_transfer(const name& from,const name& to,const asset& quantity, const string& memo) {
    if (from == get_self() || to != get_self()) return;
    CHECKC(!_gstate.paused, err::PAUSED, "market paused");
    CHECKC(quantity.amount > 0, err::NOT_POSITIVE, "invalid amount");

    auto parts = split(memo, ":");
    // -------- supply --------
    if (parts[0] == "supply") {
        _on_supply(from, quantity);
        return;
    }

    // repay:<borrower>
    if (parts[0] == "repay") {
        CHECKC(parts.size() == 2, err::PARAM_ERROR, "invalid repay memo");
        name borrower{ parts[1].c_str() };
        check(is_account(borrower), "borrower not exists");
        _on_repay(from, borrower, quantity);
        return;
    }

    // liquidate:<borrower>:<DEBT>:<COLL>
    // DEBT = 被偿还的债务资产（repay asset）
    // COLL = 被扣走的抵押资产（seize asset）
    if (parts[0] == "liquidate") {
        CHECKC(parts.size() == 4, err::PARAM_ERROR, "invalid liquidate memo");

        name borrower{ parts[1].c_str() };
        check(is_account(borrower), "borrower not exists");
        symbol_code debt_sym(parts[2]);
        symbol_code coll_sym(parts[3]);

        _on_liquidate(from, borrower, debt_sym, quantity, coll_sym);
        return;
    }

    CHECKC(false, err::PARAM_ERROR, "unknown transfer memo");
}

void tyche_market::_on_supply(const name& owner, const asset& quantity) {
    action_ctx ctx{ .now = current_time_point() };

    reserves_t reserves(get_self(), get_self().value);
    positions_t positions(get_self(), owner.value);

    auto& res = _get_reserve(ctx, reserves, quantity.symbol.code());
    check(res.token_contract == get_first_receiver(), "invalid token");
    check(!res.paused, "reserve paused");

    auto pos_ptr = _get_or_create_position(positions, res, quantity.symbol.code());
    auto pos_itr = positions.find(pos_ptr->sym_code.raw());

    position_row pos = *pos_itr;

    // settle supply interest（基于 ctx snapshot）
    _settle_supply_interest(pos, res);
    asset share_delta = _supply_shares_from_amount(quantity, res.total_supply_shares,res.total_liquidity);
    pos.supply_shares += share_delta;

    // commit
    positions.modify(pos_itr, same_payer, [&](auto& r){ r = pos; });
    reserves.modify(reserves.find(res.sym_code.raw()), same_payer, [&](auto& r){
        r = res;
        r.total_liquidity     += quantity;
        r.total_supply_shares += share_delta;
    });
}

position_row* tyche_market::_get_or_create_position(positions_t& table,const reserve_state& res,symbol_code sym) {
    auto itr = table.find(sym.raw());
    if (itr != table.end()) {
        return const_cast<position_row*>(&(*itr));
    }

    const symbol canonical_sym = res.total_liquidity.symbol;

    table.emplace(get_self(), [&](auto& row) {
        row.sym_code = sym;

        // supply side
        row.supply_shares = asset(0, canonical_sym);

        // borrow side
        row.borrow.borrow_scaled     = 0;
        row.borrow.accrued_interest  = 0;
        row.borrow.last_borrow_index = 0;
        row.borrow.id                = 0;
        row.borrow.last_updated      = current_time_point();

        // supply interest
        row.supply_interest.pending_interest = asset(0, canonical_sym);
        row.supply_interest.claimed_interest = asset(0, canonical_sym);
        row.supply_interest.id               = res.supply_index.id;
        row.supply_interest.last_reward_per_share = res.supply_index.reward_per_share;

        // collateral
        row.collateral = false;
    });

    auto new_itr = table.find(sym.raw());
    check(new_itr != table.end(), "failed to create position");
    return const_cast<position_row*>(&(*new_itr));
}
void tyche_market::_on_repay(const name& payer,const name& borrower,const asset& quantity) {
    check(quantity.amount > 0, "repay must be positive");
    action_ctx ctx{ .now = current_time_point() };
    const symbol_code sym = quantity.symbol.code();

    reserves_t  reserves(get_self(), get_self().value);
    positions_t positions(get_self(), borrower.value);

    // 获取已推进的 debt reserve（语义唯一）
    reserve_state& res = _get_reserve(ctx, reserves, sym);
    check(res.token_contract == get_first_receiver(), "invalid token contract");
    // load position
    auto pos_itr    =  positions.find(sym.raw());
    check(pos_itr   != positions.end(), "no debt position");

    position_row pos = *pos_itr;

    // repay（内部会先 settle borrow interest）
    repay_result rr = _repay_by_snapshot(res, pos, quantity.amount, /*do_settle=*/true);
    check(rr.paid > 0, "repay too small");
    if (pos.borrow.borrow_scaled == 0) {
        pos.borrow.accrued_interest  = 0;
        pos.borrow.last_borrow_index = 0;
        pos.borrow.id                = 0;
    }

    // 更新利率（action 内即可）
    _update_borrow_rate(res);
    // commit
    positions.modify(pos_itr, same_payer, [&](auto& r){
        r = pos;
    });

    _flush_reserve(ctx, reserves, sym);
    // ⑥ refund
    if (rr.refund > 0) {
        _transfer_out( res.token_contract, payer, asset(rr.refund, quantity.symbol), "repay refund");
    }

}

void tyche_market::_on_liquidate(const name& liquidator,const name& borrower,const symbol_code& debt_sym,const asset& repay_amount,const symbol_code& coll_sym) {

    check(repay_amount.amount > 0, "repay > 0");
    action_ctx ctx{ .now = current_time_point() };

    reserves_t  reserves(get_self(), get_self().value);
    positions_t positions(get_self(), borrower.value);
    prices_t    prices(get_self(), get_self().value);

    auto& debt_res = _get_reserve(ctx, reserves, debt_sym);
    auto& coll_res = _get_reserve(ctx, reserves, coll_sym);
    check(debt_res.token_contract == get_first_receiver(), "invalid debt token contract");

    auto debt_pos_itr = positions.find(debt_sym.raw());
    auto coll_pos_itr = positions.find(coll_sym.raw());
    check(debt_pos_itr != positions.end(), "no debt position");
    check(coll_pos_itr != positions.end(), "no collateral position");
    check(coll_pos_itr->collateral, "collateral disabled");
    check(coll_pos_itr->supply_shares.amount > 0, "no collateral supply");

    position_row debt_pos = *debt_pos_itr;
    position_row coll_pos = *coll_pos_itr;

    asset debt_price = _get_fresh_price(prices, debt_sym);
    asset coll_price = _get_fresh_price(prices, coll_sym);

    liquidate_result lr = _liquidate_internal(debt_res, coll_res,debt_pos, coll_pos,repay_amount.amount,debt_price, coll_price);

    // 1) commit positions
    positions.modify(debt_pos_itr, same_payer, [&](auto& r){ r = debt_pos; });
    positions.modify(coll_pos_itr, same_payer, [&](auto& r){ r = coll_pos; });

    // 2) flush 两个 reserve（顺序无关）
    _flush_reserve(ctx, reserves, debt_sym);
    _flush_reserve(ctx, reserves, coll_sym);

    // 3) refund（多余的 debt token 退给清算人）
    if (lr.refund > 0) {
        check(repay_amount.symbol == debt_res.total_liquidity.symbol,"refund token must be debt token");
        check(repay_amount.symbol.code() == debt_sym,"repay symbol must match debt_sym");
        _transfer_out(debt_res.token_contract,liquidator,asset(lr.refund, repay_amount.symbol),"liquidate refund");
    }
    // 4) seize collateral token 给清算人
    _transfer_out(coll_res.token_contract,liquidator,asset(lr.seized, coll_res.total_liquidity.symbol),"liquidate seize");

}
// 在一个确定时间截面内，把 一笔还债 精确地转化为 等值（含奖励）的抵押物扣减，并严格维护池子与仓位的不变量
liquidate_result tyche_market::_liquidate_internal(reserve_state& debt_res,
                                                    reserve_state& coll_res,
                                                    position_row&  debt_pos,
                                                    position_row&  coll_pos,
                                                    int64_t        repay_amount,   // 用户转进来的最大愿付额（debt token）
                                                    asset          debt_price,
                                                    asset          coll_price) {
    liquidate_result out{};

    check(repay_amount > 0, "repay amount must be positive");
    check(debt_price.symbol == USDT_SYM, "debt price must be USDT");
    check(coll_price.symbol == USDT_SYM, "coll price must be USDT");
    check(debt_price.amount > 0, "invalid debt price");
    check(coll_price.amount > 0, "invalid coll price");

    // 0) 清算截面内：仅在这里 settle 一次
    int64_t old_ai = debt_pos.borrow.accrued_interest;
    _settle_borrow_interest(debt_res, debt_pos);
    int64_t delta_ai = debt_pos.borrow.accrued_interest - old_ai;

    if (delta_ai > 0) {
        _safe_add_i64(debt_res.total_accrued_interest, delta_ai, "total_accrued_interest overflow");
    }

    // int64_t principal_amt = _amount_from_scaled(debt_pos.borrow.borrow_scaled, HIGH_PRECISION);
    int64_t principal_amt = _amount_from_scaled(debt_pos.borrow.borrow_scaled, debt_res.borrow_index.index);
    int64_t debt_before   = debt_pos.borrow.accrued_interest + principal_amt;
    check(debt_before > 0, "no debt");

    // 1) close factor clamp
    int64_t max_close = (int64_t)((__int128)debt_before * (__int128)_gstate.close_factor_bp / (__int128)RATE_SCALE);
    check(max_close > 0, "close factor too small");

    int64_t actual = std::min(std::min(repay_amount, debt_before), max_close);
    check(actual > 0, "repay too small");

    // 2) 偿还（do_settle=false，避免重复结息）
    repay_result rr = _repay_by_snapshot(debt_res, debt_pos, actual, /*do_settle=*/false);
    check(rr.paid > 0, "repay failed");

    out.paid   = rr.paid;
    out.refund = repay_amount - rr.paid;      // ✅ 多余都退给清算人（实际转入 - 实际消耗）
    if (out.refund < 0) out.refund = 0;

    // 3) repaid value（USDT）
    asset repaid_asset(out.paid, debt_res.total_liquidity.symbol);
    int128_t repay_value = value_of(repaid_asset, debt_price);
    check(repay_value > 0, "repay value too small");

    // 4) bonus
    uint64_t bonus_bp = coll_res.liquidation_bonus;
    if (_gstate.emergency_mode) {
        bonus_bp = std::min<uint64_t>( RATE_SCALE + _gstate.max_emergency_bonus_bp,bonus_bp + _gstate.emergency_bonus_bp);
    }
    check(bonus_bp >= RATE_SCALE, "invalid liquidation bonus");

    int128_t seize_value = repay_value * (int128_t)bonus_bp / (int128_t)RATE_SCALE;
    check(seize_value > 0, "seize value too small");

    // 5) USDT value -> collateral amount
    symbol coll_sym = coll_res.total_liquidity.symbol;
    int128_t seize_amt = seize_value * pow10_i128(coll_sym.precision()) / (int128_t)coll_price.amount;
    check(seize_amt > 0, "seize too small");
    check(seize_amt <= (int128_t)std::numeric_limits<int64_t>::max(), "seize overflow");

    asset seize_asset((int64_t)seize_amt, coll_sym);

    // 6) 扣 collateral shares（ceil）
    asset share_delta = _withdraw_shares_from_amount(seize_asset,coll_res.total_supply_shares,coll_res.total_liquidity);
    check(share_delta.amount > 0, "share delta too small");
    check(share_delta.amount <= coll_pos.supply_shares.amount, "exceeds collateral shares");

    coll_pos.supply_shares       -= share_delta;
    coll_res.total_supply_shares -= share_delta;
    coll_res.total_liquidity     -= seize_asset;

    // 7) 清算后刷新利率（debt_res）
    _update_borrow_rate(debt_res);
    out.seized = seize_asset.amount;

    return out;
}

void tyche_market::_check_price_available(symbol_code sym) const {
   prices_t prices(get_self(), get_self().value);
   _get_fresh_price(prices, sym);
}

// 用户存款利息结算（指数差值 × 份额）
void tyche_market::_settle_supply_interest(position_row& pos, const reserve_state& res) {
    auto& pool = res.supply_index;
    auto& user = pos.supply_interest;

    // === ① 首次结算：只建立锚点（以 id 为准）===
    if (user.id == 0) {
        user.id = pool.id;
        user.last_reward_per_share = pool.reward_per_share;
        return;
    }

    // === ② 若 index 未变化，可直接快进锚点 ===
    if (user.id == pool.id) {
        user.last_reward_per_share = pool.reward_per_share;
        return;
    }

    // === ③ 计算 delta_rps ===
    int128_t delta_rps = (int128_t)pool.reward_per_share - (int128_t)user.last_reward_per_share;

    // === ④ 结算利息 ===
    if (delta_rps > 0 && pos.supply_shares.amount > 0) {
        int128_t pending = delta_rps * (int128_t)pos.supply_shares.amount / HIGH_PRECISION;
        if (pending > 0) {
            check(user.pending_interest.amount <= std::numeric_limits<int64_t>::max() - (int64_t)pending,"interest overflow");
            user.pending_interest.amount += (int64_t)pending;
        }
    }

    // === ⑤ 推进用户锚点 ===
    user.last_reward_per_share = pool.reward_per_share;
    user.id = pool.id;
}

void tyche_market::_settle_borrow_interest(const reserve_state& res,position_row& pos) {
    auto& b = pos.borrow;

    // ① 无本金：只同步锚点
    if (b.borrow_scaled <= 0) {
        b.accrued_interest  = 0;
        b.last_borrow_index = 0;
        b.id                = res.borrow_index.id;
        return;
    }

    // ② 首次结算：建立锚点
    if (b.last_borrow_index == 0) {
        b.last_borrow_index = res.borrow_index.index;
        b.id                = res.borrow_index.id;
        return;
    }

    // ③ index 未变化：快进锚点
    if (b.id == res.borrow_index.id) {
        b.last_borrow_index = res.borrow_index.index;
        return;
    }

    // ④ 计算 delta index
    int128_t delta_index =
        (int128_t)res.borrow_index.index -
        (int128_t)b.last_borrow_index;

    if (delta_index <= 0) {
        b.last_borrow_index = res.borrow_index.index;
        b.id                = res.borrow_index.id;
        return;
    }

    // ⑤ 计算利息
    int128_t interest_i128 =
        (int128_t)b.borrow_scaled * delta_index / HIGH_PRECISION;

    if (interest_i128 > 0) {
        check(
            interest_i128 <= (int128_t)std::numeric_limits<int64_t>::max(),
            "borrow interest overflow"
        );
        b.accrued_interest += (int64_t)interest_i128;
    }

    // ⑥ 推进锚点
    b.last_borrow_index = res.borrow_index.index;
    b.id                = res.borrow_index.id;
}

uint64_t tyche_market::_buffer_bps_by_util(uint64_t util_bps) const {
    if (util_bps > RATE_SCALE) util_bps = RATE_SCALE;

    if (util_bps < 7000) return 100;   // <70%  => 1%
    if (util_bps < 8500) return 200;   // 70-85 => 2%
    if (util_bps < 9500) return 500;   // 85-95 => 5%
    return 1000;                       // >=95% => 10%
}

//当前时刻的借款利率是多少
void tyche_market::_update_borrow_rate(reserve_state& res) {
    uint64_t util = _util_bps(res);
    int64_t applied = _calc_borrow_rate(res, util);

    if (res.borrow_index.borrow_rate_bp == (uint64_t)applied) {
        return;
    }

    res.borrow_index.borrow_rate_bp = (uint64_t)applied;

}
//在给定目标利率的前提下，结合“上一时刻利率 + 单次变化上限 + 安全护栏”，计算当前 action 实际采用的借款利率
int64_t tyche_market::_calc_borrow_rate(const reserve_state& res, uint64_t util_bps) const {
    int64_t target = _calc_target_borrow_rate(res, util_bps);

    // 统一：borrow_rate_bp 既是当前利率，也是 last
    int64_t last =  (res.borrow_index.borrow_rate_bp == 0)
                    ? (int64_t)res.r0
                    : (int64_t)res.borrow_index.borrow_rate_bp;

    int64_t step = (int64_t)res.max_rate_step_bp;
    if (step <= 0) return target;

    int64_t delta = target - last;
    if (delta >  step) delta =  step;
    if (delta < -step) delta = -step;

    int64_t applied = last + delta;

    // guardrail
    if (applied < (int64_t)res.r0)   applied = (int64_t)res.r0;
    if (applied > (int64_t)res.r_max) applied = (int64_t)res.r_max;

    return applied;
}
// 在某个 utilization 下，借款人理论上应该付多少年化利率
int64_t tyche_market::_calc_target_borrow_rate(const reserve_state& res,uint64_t util_bps) const {
    check(res.u_opt > 0 && res.u_opt < RATE_SCALE, "u_opt must be in (0, 100%)");
    check(res.r0 <= res.r_opt && res.r_opt <= res.r_max, "invalid interest rate curve");

    if (util_bps <= res.u_opt) {
        int128_t slope = (int128_t)(res.r_opt - res.r0) * util_bps / res.u_opt;
        return res.r0 + (int64_t)slope;
    }

    uint64_t capped = util_bps > RATE_SCALE ? RATE_SCALE : util_bps;
    uint64_t denom = RATE_SCALE - res.u_opt;
    check(denom > 0, "invalid u_opt");

    uint64_t excess = capped - res.u_opt;
    int128_t slope = (int128_t)(res.r_max - res.r_opt) * excess / denom;
    return res.r_opt + (int64_t)slope;
}

//借款人债务如何随时间增长
void tyche_market::_accrue_borrow_index(reserve_state& res, time_point_sec now) {
    auto& idx = res.borrow_index;
    if (now <= idx.last_updated) return;

    uint32_t dt = now.sec_since_epoch() - idx.last_updated.sec_since_epoch();
    if (dt == 0 || res.total_borrow_scaled == 0) {
        idx.last_updated = now;
        return;
    }

    int128_t delta = (int128_t)idx.index * idx.borrow_rate_bp * dt / (RATE_SCALE * SECONDS_PER_YEAR);

    if (delta > 0) {
        idx.index += (uint128_t)delta;
        idx.id += 1;   // 真实产生了利息

        int128_t interest = res.total_borrow_scaled * delta / HIGH_PRECISION;
        res.total_accrued_interest += (int64_t)interest;
    }

    idx.last_updated = now;
}
// 把已经产生的利息，分配到“每一份 supply share”上
void tyche_market::_accrue_supply_index(reserve_state& res, time_point_sec now) {
    auto& sidx = res.supply_index;

    if (now <= sidx.last_updated) return;

    // 没有供应份额，无法分发
    if (res.total_supply_shares.amount == 0) {
        sidx.last_updated = now;
        return;
    }

    // 当前「可分配的真实利息现金」
    int64_t available = res.interest_realized.amount - res.interest_claimed.amount;

    if (available <= 0) {
        sidx.last_updated = now;
        return;
    }

    // ⭐ 只分发“增量部分”
    int64_t delta_available = available - sidx.indexed_available;
    if (delta_available <= 0) {
        // 没有新的可分配利息，禁止重复分发
        sidx.last_updated = now;
        return;
    }

    // 计算本次 rps 增量
    int128_t delta_rps = (int128_t)delta_available * HIGH_PRECISION / res.total_supply_shares.amount;

    if (delta_rps > 0) {
        sidx.reward_per_share += (uint128_t)delta_rps;
        sidx.id += 1;  // 进入新的 supply 分发 epoch
        sidx.indexed_available = available; // ⭐ 记账到这里为止
    }

    sidx.last_updated = now;
}
//把一笔还款金额严格按「先利息、后本金」规则结算，同步维护用户仓位与资金池的所有会计不变量，并返回“实际支付了多少 / 剩余退款多少
repay_result tyche_market::_repay_by_snapshot(reserve_state& res,position_row&  pos,int64_t pay_amount, bool do_settle) {
    repay_result rr{};
    check(pay_amount > 0, "pay_amount must be positive");

    // 0) 只在这里结一次用户利息
    if (do_settle) {
        _settle_borrow_interest(res, pos);
    }

    const int64_t principal_amt =
        _amount_from_scaled(pos.borrow.borrow_scaled, res.borrow_index.index);

    rr.debt_before = pos.borrow.accrued_interest + principal_amt;
    if (rr.debt_before <= 0) {
        rr.refund = pay_amount;
        return rr;
    }

    int64_t pay_left = pay_amount;

    // 1) 先还利息
    int64_t pay_interest = std::min(pay_left, pos.borrow.accrued_interest);
    if (pay_interest > 0) {
        pos.borrow.accrued_interest -= pay_interest;

        _safe_sub_i64(res.total_accrued_interest, pay_interest,"total_accrued_interest underflow");
        res.interest_realized.amount += pay_interest;

        rr.paid += pay_interest;
        pay_left -= pay_interest;
    }

    // 2) 再还本金（scaled）
    if (pay_left > 0 && pos.borrow.borrow_scaled > 0) {
        int128_t scaled_delta = _scaled_from_amount(pay_left, res.borrow_index.index);
        if (scaled_delta > pos.borrow.borrow_scaled)
            scaled_delta = pos.borrow.borrow_scaled;

        int64_t pay_principal = _amount_from_scaled(scaled_delta, res.borrow_index.index);

        if (pay_principal > 0) {
            pos.borrow.borrow_scaled -= scaled_delta;
            res.total_borrow_scaled  -= scaled_delta;
            res.total_liquidity.amount += pay_principal;

            rr.scaled_delta = scaled_delta;
            rr.paid += pay_principal;
            pay_left -= pay_principal;
        }
    }

    rr.refund = pay_left > 0 ? pay_left : 0;
    pos.borrow.last_updated = current_time_point();

    if (pos.borrow.borrow_scaled == 0) {
        pos.borrow.accrued_interest  = 0;
        pos.borrow.last_borrow_index = 0;
        pos.borrow.id                = res.borrow_index.id;
    }

    return rr;
}

uint64_t tyche_market::_util_bps(const reserve_state& res) const {
    int128_t debt =
        (int128_t)res.total_borrow_scaled
        * (int128_t)res.borrow_index.index
        / (int128_t)HIGH_PRECISION
        + (int128_t)res.total_accrued_interest;

    int128_t cash = (int128_t)res.total_liquidity.amount;
    if (debt + cash == 0) return 0;

    return (uint64_t)(debt * RATE_SCALE / (debt + cash));
}

int128_t tyche_market::_scaled_from_amount(int64_t amt, uint128_t idx) const {
    return (int128_t)amt * HIGH_PRECISION / idx;
}

int64_t tyche_market::_amount_from_scaled(int128_t scaled, uint128_t idx) const {
    return (int64_t)(scaled * idx / HIGH_PRECISION);
}

tyche_market::valuation tyche_market::_compute_valuation(action_ctx& ctx,name owner,const position_row* override_pos,symbol_code override_sym) {
    valuation v{};

    prices_t    prices(get_self(), get_self().value);
    positions_t positions(get_self(), owner.value);

    bool seen_override = false;

    auto apply_one = [&](const position_row& pos) {
        const uint64_t key = pos.sym_code.raw();

        auto rc = ctx.reserve_cache.find(key);
        if (rc == ctx.reserve_cache.end()) return;

        const reserve_state& res = rc->second;

        // ---- debt ----
        if (pos.borrow.borrow_scaled > 0 || pos.borrow.accrued_interest > 0) {
            int64_t principal = _amount_from_scaled(pos.borrow.borrow_scaled, res.borrow_index.index);
            int64_t debt_amt  = principal + pos.borrow.accrued_interest;

            if (debt_amt > 0) {
                asset price = _get_fresh_price(prices, pos.sym_code);
                v.debt_value += value_of(asset(debt_amt, res.total_liquidity.symbol), price);
            }
        }

        // ---- collateral ----
        if (pos.collateral && pos.supply_shares.amount > 0 && res.max_ltv > 0) {
            asset price      = _get_fresh_price(prices, pos.sym_code);
            asset supply_amt = _amount_from_shares(pos.supply_shares, res.total_supply_shares, res.total_liquidity);

            int128_t value = value_of(supply_amt, price);
            v.collateral_value      += value * res.liquidation_threshold / RATE_SCALE;
            v.max_borrowable_value  += value * res.max_ltv / RATE_SCALE;
        }
    };

    for (auto it = positions.begin(); it != positions.end(); ++it) {
        const position_row& pos =
            (override_pos != nullptr && it->sym_code == override_sym)
            ? (seen_override = true, *override_pos)
            : *it;

        apply_one(pos);
    }

    // ⭐ 关键补丁：第一次借该币种，没有 position 行时，也要把 override_pos 算进去
    if (override_pos != nullptr && !seen_override) {
        apply_one(*override_pos);
    }

    return v;
}

static std::string i128_to_string(int128_t value) {
    if (value == 0) return "0";

    bool neg = value < 0;
    if (neg) value = -value;

    std::string s;
    while (value > 0) {
        int digit = (int)(value % 10);
        s.push_back('0' + digit);
        value /= 10;
    }

    if (neg) s.push_back('-');
    std::reverse(s.begin(), s.end());
    return s;
}

void tyche_market::_check_health_factor( action_ctx& ctx,name owner,const position_row* override_pos,symbol_code override_sym) {
    valuation v = _compute_valuation(ctx, owner, override_pos, override_sym);
    // check(
    //     false,
    //     (
    //         std::string("HF DEBUG coll=") + i128_to_string(v.collateral_value) +
    //         " debt=" + i128_to_string(v.debt_value) +
    //         " max="  + i128_to_string(v.max_borrowable_value)
    //     ).c_str()
    // );
    if (v.debt_value == 0) return;

    // 1) 清算安全线（liquidation_threshold）
    check(v.collateral_value >= v.debt_value,"health factor below 1");

    // 2) 借款上限线（max_ltv），防止借到清算线才被拒
    check(v.debt_value <= v.max_borrowable_value, "exceeds max LTV");
}

asset tyche_market::_get_fresh_price(prices_t& prices, symbol_code sym) const {
    if (sym == USDT_SYM.code()) {
        return asset((int64_t)pow10(USDT_SYM.precision()), USDT_SYM);
    }

    auto itr = prices.find(sym.raw());
    check(itr != prices.end(), "price not found");

    auto now = current_time_point();
    auto age = now - itr->updated_at;

    int64_t ttl_us = (int64_t)_gstate.price_ttl_sec * 1'000'000;
    if (_gstate.emergency_mode) ttl_us *= 2;

    check(age.count() <= ttl_us, "price stale");
    check(itr->price.symbol == USDT_SYM, "price must be USDT");
    check(itr->price.amount > 0, "invalid price");

    return itr->price;
}

// 根据 shares 数量，算出对应的 amount 数量
asset tyche_market::_amount_from_shares(const asset& shares,const asset& total_shares,const asset& total_amount) const {
    if (shares.amount == 0 || total_shares.amount == 0) {
        return asset(0, total_amount.symbol);
    }

    int128_t num = (int128_t)shares.amount * total_amount.amount;
    int64_t amt = (int64_t)(num / total_shares.amount);

    return asset(amt, total_amount.symbol);
}
// 用户想拿走 amount，必须至少付出足够多的 shares，宁多不少
asset tyche_market::_withdraw_shares_from_amount(const asset& amount,const asset& total_shares,const asset& total_amount) const {
    check(amount.symbol == total_amount.symbol, "symbol mismatch");
    check(total_amount.amount > 0 && total_shares.amount > 0, "invalid pool");

    int128_t num = (int128_t)amount.amount * total_shares.amount;
    int128_t den = total_amount.amount;

    int64_t shares = (int64_t)((num + den - 1) / den); // ceil
    check(shares > 0, "withdraw too small");

    return asset(shares, amount.symbol);
}

asset tyche_market::_supply_shares_from_amount(const asset& amount,const asset& total_shares,const asset& total_amount) const {
    check(amount.symbol == total_amount.symbol, "symbol mismatch");

    if (total_amount.amount == 0 || total_shares.amount == 0) {
        return amount; // 1:1
    }

    int128_t num = (int128_t)amount.amount * total_shares.amount;
    int64_t shares = (int64_t)(num / total_amount.amount);
    check(shares > 0, "supply too small");

    return asset(shares, amount.symbol);
}

void tyche_market::_transfer_out(name token_contract,name to,const asset& quantity, const string& memo) {
    if (quantity.amount <= 0) return;

    TRANSFER(token_contract, to, quantity, memo);
}

// action 内唯一时间锚点 ctx.now
// 只负责：加载 -> 推进指数 -> refresh total_debt -> cache
// 不在这里 reserves.modify，避免“先写一次旧snap，后面 action 又改 res 导致 total_debt/borrow_scaled 不一致”
reserve_state& tyche_market::_get_reserve(action_ctx& ctx, reserves_t& reserves, symbol_code sym) {
    const uint64_t key = sym.raw();
    if (auto it = ctx.reserve_cache.find(key); it != ctx.reserve_cache.end()) return it->second;

    auto itr = reserves.find(key);
    check(itr != reserves.end(), "reserve not found");

    reserve_state snap = *itr;
    const time_point_sec& now = ctx.now;

    // 1) 用“旧 borrow_rate_bp”把 [last_updated, now) 的历史利息结算掉
    _accrue_borrow_index(snap, now);      // ✅ 这里如果 delta>0，idx.id++（利息epoch）

    // 2) 历史结算完毕后，再根据当前util算出“新利率”，用于下一段时间
    _update_borrow_rate(snap);            // ✅ 这里只改 borrow_rate_bp（不改 id）

    // 3) supply 分发（也可放前后，只要语义一致）
    _accrue_supply_index(snap, now);

    auto [inserted_it, ok] = ctx.reserve_cache.emplace(key, snap);
    return inserted_it->second;
}

// 在不写任何用户状态、不结息、不真实修改仓位的前提下，假设“某个仓位发生了一次变化”，并验证这次变化是否仍然满足 Health Factor（HF ≥ 1）
void tyche_market::_simulate_position_change(action_ctx& ctx,name owner,reserves_t& reserves,positions_t& positions,symbol_code sym,const position_change& change) {
    // ① 推进 owner 相关的所有 reserve（语义唯一）
    for (auto it = positions.begin(); it != positions.end(); ++it) {
        _get_reserve(ctx, reserves, it->sym_code);
    }
    reserve_state& res = _get_reserve(ctx, reserves, sym);

    // ② 构造模拟仓位（不写表）
    position_row sim_pos{};
    auto pos_itr = positions.find(sym.raw());
    if (pos_itr != positions.end()) {
        sim_pos = *pos_itr;
    } else {
        // 纯影子仓位
        sim_pos.sym_code = sym;
        sim_pos.supply_shares = asset(0, res.total_liquidity.symbol);

        sim_pos.borrow.borrow_scaled     = 0;
        sim_pos.borrow.accrued_interest  = 0;
        sim_pos.borrow.last_borrow_index = 0;
        sim_pos.borrow.id                = 0;   // 影子仓位不继承 epoch

        sim_pos.collateral = false;
    }

    // ③ 影子结息（只在已有本金时才有意义）
    if (sim_pos.borrow.borrow_scaled > 0) {
        _settle_borrow_interest(res, sim_pos);
    }

    // ④ 应用 borrow 变化（scaled）
    if (change.borrow_scaled_delta != 0) {
        sim_pos.borrow.borrow_scaled += change.borrow_scaled_delta;
        check(sim_pos.borrow.borrow_scaled >= 0, "borrow underflow");
    }

    // ⑤ 应用 supply shares 变化
    if (change.supply_shares_delta != 0) {
        int128_t new_amt = (int128_t)sim_pos.supply_shares.amount + change.supply_shares_delta;
        check(new_amt >= 0, "supply underflow");
        sim_pos.supply_shares.amount = (int64_t)new_amt;
    }

    // ⑥ collateral override（三态）
    if (change.collateral_override.has_value()) {
        sim_pos.collateral = *change.collateral_override;
    }

    // ⑦ HF 校验（唯一出口）
    _check_health_factor(ctx, owner, &sim_pos, sym);
}
// 用户真实债务
int64_t tyche_market::_user_real_debt_amt(const reserve_state& res,const position_row& pos) const {
    int64_t principal = _amount_from_scaled(pos.borrow.borrow_scaled, res.borrow_index.index);

    int64_t debt = principal + pos.borrow.accrued_interest;

    // 展示安全：逻辑上不应该 < 0
    return debt > 0 ? debt : 0;
}
// 池子真实总债务（UI / 总览专用）
int64_t tyche_market::_reserve_real_total_debt_amt(const reserve_state& res) const {
    int128_t debt =
        (int128_t)res.total_borrow_scaled
        * (int128_t)res.borrow_index.index
        / (int128_t)HIGH_PRECISION
        + (int128_t)res.total_accrued_interest;
    check(debt <= (int128_t)std::numeric_limits<int64_t>::max(), "overflow");
    return (int64_t)debt;
}

void tyche_market::_flush_reserve(action_ctx& ctx, reserves_t& reserves, symbol_code sym) {
    const uint64_t key = sym.raw();

    auto it = ctx.reserve_cache.find(key);
    if (it == ctx.reserve_cache.end()) return;

    auto itr = reserves.find(key);
    check(itr != reserves.end(), "reserve not found");

    // 落盘前再 refresh 一次，保证 borrow/repay/liquidate 修改后 total_debt 一定正确
    it->second.total_debt.amount = _reserve_real_total_debt_amt(it->second);

    reserves.modify(itr, same_payer, [&](auto& r) {
        r = it->second;
    });
}


} // namespace tychefi
