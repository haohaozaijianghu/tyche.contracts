#include <tyche.market/tyche.market.hpp>

#include <cmath>
#include <limits>
#include <tuple>

namespace tychefi {

static constexpr int128_t MAX_RATE_DELTA = static_cast<int128_t>(std::numeric_limits<int64_t>::max());

tyche_market::tyche_market(name receiver, name code, datastream<const char*> ds)
   : contract(receiver, code, ds) {
   _gstate = _read_state();
}

void tyche_market::init(name admin) {
   require_auth(get_self());
   check(admin != name(), "admin cannot be empty");
   _gstate.admin = admin;
   _write_state(_gstate);
}

void tyche_market::setpause(bool paused) {
   require_auth(_gstate.admin);
   _gstate.paused = paused;
   _write_state(_gstate);
}

void tyche_market::setpricettl(uint32_t ttl_sec) {
   require_auth(_gstate.admin);
   check(ttl_sec > 0, "ttl must be positive");
   _gstate.price_ttl_sec = ttl_sec;
   _write_state(_gstate);
}

void tyche_market::setclosefac(uint64_t close_factor_bp) {
   require_auth(_gstate.admin);
   check(close_factor_bp > 0 && close_factor_bp <= RATE_SCALE, "close factor must be within 0-100% in bps");
   _gstate.close_factor_bp = close_factor_bp;
   _write_state(_gstate);
}

void tyche_market::setprice(symbol_code sym, uint64_t price) {
   require_auth(_gstate.admin);
   check(price > 0, "price must be positive");

   prices_t prices(get_self(), get_self().value);
   auto     itr = prices.find(sym.raw());
   if (itr == prices.end()) {
      prices.emplace(get_self(), [&](auto& row) {
         row.sym_code   = sym;
         row.price      = price;
         row.updated_at = current_time_point();
      });
   } else {
      prices.modify(itr, get_self(), [&](auto& row) {
         row.price      = price;
         row.updated_at = current_time_point();
      });
   }
}

void tyche_market::addreserve(const extended_symbol& asset_sym,
                              uint64_t max_ltv,
                              uint64_t liq_threshold,
                              uint64_t liq_bonus,
                              uint64_t reserve_factor,
                              uint64_t u_opt,
                              uint64_t r0,
                              uint64_t r_opt,
                              uint64_t r_max) {
   require_auth(_gstate.admin);
   check(!_gstate.paused, "market paused");
   check(max_ltv <= RATE_SCALE, "max_ltv too large");
   check(liq_threshold <= RATE_SCALE, "liquidation threshold too large");
   check(liq_bonus >= RATE_SCALE && liq_bonus <= RATE_SCALE * 2, "liquidation bonus must be between 1x and 2x");
   check(reserve_factor <= RATE_SCALE, "reserve factor too large");

   reserves_t reserves(get_self(), get_self().value);
   check(reserves.find(asset_sym.get_symbol().code().raw()) == reserves.end(), "reserve already exists");

   reserves.emplace(get_self(), [&](auto& row) {
      row.sym_code            = asset_sym.get_symbol().code();
      row.token_contract      = asset_sym.get_contract();
      row.max_ltv             = max_ltv;
      row.liquidation_threshold = liq_threshold;
      row.liquidation_bonus   = liq_bonus;
      row.reserve_factor      = reserve_factor;
      row.u_opt               = u_opt;
      row.r0                  = r0;
      row.r_opt               = r_opt;
      row.r_max               = r_max;
      row.total_liquidity     = asset(0, asset_sym.get_symbol());
      row.total_debt          = asset(0, asset_sym.get_symbol());
      row.total_supply_shares = asset(0, asset_sym.get_symbol());
      row.total_borrow_shares = asset(0, asset_sym.get_symbol());
      row.protocol_reserve    = asset(0, asset_sym.get_symbol());
      row.last_updated        = current_time_point();
      row.paused              = false;
   });
}

void tyche_market::supply(name owner, asset quantity) {
   require_auth(owner);
   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto       res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   _check_price_available(quantity.symbol.code());

   auto res = _accrue(*res_itr);

   asset shares = _supply_shares_from_amount(quantity, res.total_supply_shares, res.total_liquidity);

   _transfer_from(res.token_contract, owner, quantity, "supply");

   positions_t positions(get_self(), get_self().value);
   auto        pos_ptr = _get_or_create_position(positions, owner, quantity.symbol.code(), quantity);
   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.supply_shares += shares;
      row.collateral     = true; // default to enabled after supply
   });

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
   auto       res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   _check_price_available(quantity.symbol.code());

   positions_t positions(get_self(), get_self().value);
   auto        owner_idx = positions.get_index<"ownerreserve"_n>();
   auto        pos_itr   = owner_idx.find(((uint128_t)owner.value << 64) | quantity.symbol.code().raw());
   check(pos_itr != owner_idx.end(), "no position found");
   check(pos_itr->supply_shares.amount > 0, "no supply shares");

   auto res = _accrue(*res_itr);

   asset max_withdrawable = _amount_from_shares(pos_itr->supply_shares, res.total_supply_shares, res.total_liquidity);
   check(quantity <= max_withdrawable, "withdraw exceeds balance");

   uint64_t available = res.total_liquidity.amount - res.total_debt.amount;
   check(quantity.amount <= (int64_t)available, "insufficient liquidity");

   asset share_delta = _withdraw_shares_from_amount(quantity, res.total_supply_shares, res.total_liquidity);

   valuation val = _compute_valuation(owner);
   check(val.debt_value == 0 || val.collateral_value >= val.debt_value, "health factor below 1 after withdraw");

   _transfer_out(res.token_contract, owner, quantity, "withdraw");

   owner_idx.modify(pos_itr, owner, [&](auto& row) {
      row.supply_shares -= share_delta;
   });

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
   positions_t positions(get_self(), get_self().value);
   auto        owner_idx = positions.get_index<"ownerreserve"_n>();
   auto        pos_itr   = owner_idx.find(((uint128_t)owner.value << 64) | sym.raw());
   check(pos_itr != owner_idx.end(), "no position found");

   _check_price_available(sym);

   if (pos_itr->collateral == enabled) return;

   owner_idx.modify(pos_itr, owner, [&](auto& row) {
      row.collateral = enabled;
   });

   if (!enabled) {
      valuation val = _compute_valuation(owner);
      check(val.debt_value == 0 || val.collateral_value >= val.debt_value, "health factor below 1 after disabling collateral");
   }
}

void tyche_market::borrow(name owner, asset quantity) {
   require_auth(owner);
   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto       res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

   _check_price_available(quantity.symbol.code());

   auto res = _accrue(*res_itr);

   uint64_t available = res.total_liquidity.amount - res.total_debt.amount;
   check(quantity.amount <= (int64_t)available, "insufficient liquidity");

   positions_t positions(get_self(), get_self().value);
   auto        pos_ptr = _get_or_create_position(positions, owner, quantity.symbol.code(), quantity);

   asset borrow_shares = _borrow_shares_from_amount(quantity, res.total_borrow_shares, res.total_debt);

   valuation val = _compute_valuation(owner);
   check(val.debt_value > 0, "valuation error");
   check(val.debt_value <= val.max_borrowable_value, "exceeds max LTV");
   check(val.collateral_value >= val.debt_value, "health factor below 1 after borrow");

   _transfer_out(res.token_contract, owner, quantity, "borrow");

   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.borrow_shares += borrow_shares;
   });

   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = res.total_liquidity;
      row.total_debt          = res.total_debt + quantity;
      row.total_borrow_shares = res.total_borrow_shares + borrow_shares;
      row.total_supply_shares = res.total_supply_shares;
      row.protocol_reserve    = res.protocol_reserve;
      row.last_updated        = res.last_updated;
   });
}

void tyche_market::repay(name payer, name borrower, asset quantity) {
   require_auth(payer);
   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto       res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");

   positions_t positions(get_self(), get_self().value);
   auto        owner_idx = positions.get_index<"ownerreserve"_n>();
   auto        pos_itr   = owner_idx.find(((uint128_t)borrower.value << 64) | quantity.symbol.code().raw());
   check(pos_itr != owner_idx.end(), "no borrow position");
   check(pos_itr->borrow_shares.amount > 0, "no debt to repay");

   auto res = _accrue(*res_itr);

   asset current_debt = _amount_from_shares(pos_itr->borrow_shares, res.total_borrow_shares, res.total_debt);
   asset repay_amount = quantity;
   if (repay_amount > current_debt) {
      repay_amount = current_debt;
   }

   asset share_delta = _repay_shares_from_amount(repay_amount, res.total_borrow_shares, res.total_debt);

   _transfer_from(res.token_contract, payer, repay_amount, "repay");

   owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
      row.borrow_shares -= share_delta;
   });

   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_debt          = res.total_debt - repay_amount;
      row.total_borrow_shares = res.total_borrow_shares - share_delta;
      row.total_liquidity     = res.total_liquidity + repay_amount;
      row.total_supply_shares = res.total_supply_shares;
      row.protocol_reserve    = res.protocol_reserve;
      row.last_updated        = res.last_updated;
   });
}

void tyche_market::liquidate(name liquidator,
                             name borrower,
                             symbol_code debt_sym,
                             asset repay_amount,
                             symbol_code collateral_sym) {
   require_auth(liquidator);
   check(!_gstate.paused, "market paused");
   check(repay_amount.amount > 0, "repay amount must be positive");

   valuation val = _compute_valuation(borrower);
   check(val.debt_value > 0 && val.collateral_value < val.debt_value, "position not eligible for liquidation");

   reserves_t reserves(get_self(), get_self().value);
   auto       debt_itr = reserves.find(debt_sym.raw());
   check(debt_itr != reserves.end(), "debt reserve not found");
   auto collat_itr = reserves.find(collateral_sym.raw());
   check(collat_itr != reserves.end(), "collateral reserve not found");

   auto debt_res   = _accrue(*debt_itr);
   auto collat_res = _accrue(*collat_itr);

   positions_t positions(get_self(), get_self().value);
   auto        owner_idx = positions.get_index<"ownerreserve"_n>();
   auto        debt_pos  = owner_idx.find(((uint128_t)borrower.value << 64) | debt_sym.raw());
   auto        coll_pos  = owner_idx.find(((uint128_t)borrower.value << 64) | collateral_sym.raw());
   check(debt_pos != owner_idx.end(), "borrow position not found");
   check(coll_pos != owner_idx.end(), "collateral position not found");

   asset borrower_debt = _amount_from_shares(debt_pos->borrow_shares, debt_res.total_borrow_shares, debt_res.total_debt);
   check(borrower_debt.amount > 0, "no outstanding debt");

   if (repay_amount > borrower_debt) {
      repay_amount = borrower_debt;
   }

   // cap repayment based on configured close factor
   prices_t prices(get_self(), get_self().value);
   uint64_t debt_price = _get_fresh_price(prices, debt_sym);
   uint64_t col_price  = _get_fresh_price(prices, collateral_sym);

   int128_t debt_value = (int128_t)borrower_debt.amount * debt_price / PRICE_SCALE;
   int128_t repay_value      = (int128_t)repay_amount.amount * debt_price / PRICE_SCALE;
   int128_t max_repay_close  = debt_value * _gstate.close_factor_bp / RATE_SCALE;

   int128_t shortfall = val.debt_value - val.collateral_value; // guaranteed > 0 by eligibility
   int128_t denom     = (int128_t)RATE_SCALE * RATE_SCALE -
                        (int128_t)collat_res.liquidation_threshold * collat_res.liquidation_bonus;
   int128_t max_repay_to_one = max_repay_close;
   if (denom > 0) {
      // minimum repayment (in value) that would bring HF back to 1 considering collateral seized
      max_repay_to_one = (shortfall * (int128_t)RATE_SCALE * RATE_SCALE + denom - 1) / denom;
   }

   int128_t repay_value_cap = std::min<int128_t>(repay_value, std::min(max_repay_close, max_repay_to_one));
   check(repay_value_cap > 0, "repay amount too small");

   // adjust repay_amount to capped value
   int64_t capped_amount = static_cast<int64_t>((repay_value_cap * PRICE_SCALE + debt_price - 1) / debt_price);
   repay_amount          = asset(capped_amount, repay_amount.symbol);
   repay_value           = (int128_t)repay_amount.amount * debt_price / PRICE_SCALE;

   if (repay_amount > borrower_debt) {
      repay_amount = borrower_debt;
      repay_value  = (int128_t)repay_amount.amount * debt_price / PRICE_SCALE;
   }

   asset debt_share_delta = _repay_shares_from_amount(repay_amount, debt_res.total_borrow_shares, debt_res.total_debt);
   check(debt_share_delta.amount > 0, "repay too small");

   // collateral to seize with bonus
   int128_t seize_value = repay_value * (collat_res.liquidation_bonus) / RATE_SCALE;
   check(seize_value <= (int128_t)std::numeric_limits<int64_t>::max() * col_price / PRICE_SCALE, "seize overflow");
   int64_t  seize_amount = (int64_t)(seize_value * PRICE_SCALE / col_price);
   check(seize_amount > 0, "seize amount zero");
   asset    seize_asset  = asset(seize_amount, collat_res.total_liquidity.symbol);

   asset collateral_balance = _amount_from_shares(coll_pos->supply_shares, collat_res.total_supply_shares, collat_res.total_liquidity);
   check(collateral_balance.amount >= seize_asset.amount, "insufficient collateral");

   asset collat_share_delta = _withdraw_shares_from_amount(seize_asset, collat_res.total_supply_shares, collat_res.total_liquidity);

   _transfer_from(debt_res.token_contract, liquidator, repay_amount, "liquidate repay");
   _transfer_out(collat_res.token_contract, liquidator, seize_asset, "liquidate seize");

   owner_idx.modify(debt_pos, same_payer, [&](auto& row) {
      row.borrow_shares -= debt_share_delta;
   });

   owner_idx.modify(coll_pos, same_payer, [&](auto& row) {
      row.supply_shares -= collat_share_delta;
   });

   reserves.modify(debt_itr, same_payer, [&](auto& row) {
      row.total_debt          = debt_res.total_debt - repay_amount;
      row.total_borrow_shares = debt_res.total_borrow_shares - debt_share_delta;
      row.total_liquidity     = debt_res.total_liquidity + repay_amount;
      row.total_supply_shares = debt_res.total_supply_shares;
      row.protocol_reserve    = debt_res.protocol_reserve;
      row.last_updated        = debt_res.last_updated;
   });

   reserves.modify(collat_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = collat_res.total_liquidity - seize_asset;
      row.total_supply_shares = collat_res.total_supply_shares - collat_share_delta;
      row.total_debt          = collat_res.total_debt;
      row.total_borrow_shares = collat_res.total_borrow_shares;
      row.protocol_reserve    = collat_res.protocol_reserve;
      row.last_updated        = collat_res.last_updated;
   });
}

reserve_state tyche_market::_require_reserve(const symbol& sym) {
   reserves_t reserves(get_self(), get_self().value);
   auto       itr = reserves.find(sym.code().raw());
   check(itr != reserves.end(), "reserve not found");
   return *itr;
}

reserve_state tyche_market::_accrue(reserve_state res) {
   auto now = current_time_point();
   if (res.last_updated >= now) {
      return res;
   }

   uint32_t elapsed = (uint32_t)(now.sec_since_epoch() - res.last_updated.sec_since_epoch());
   if (elapsed == 0 || res.total_debt.amount <= 0) {
      res.last_updated = now;
      return res;
   }

   uint64_t util_bps = res.total_liquidity.amount == 0
                          ? 0
                          : static_cast<uint64_t>((int128_t)res.total_debt.amount * RATE_SCALE / res.total_liquidity.amount);
   int64_t borrow_rate_bps = _calc_borrow_rate(res, util_bps);

   int128_t interest = (int128_t)res.total_debt.amount * borrow_rate_bps * elapsed / (RATE_SCALE * SECONDS_PER_YEAR);
   if (interest > MAX_RATE_DELTA) {
      interest = MAX_RATE_DELTA;
   }

   int64_t interest_i64 = static_cast<int64_t>(interest);
   if (interest_i64 > 0) {
      res.total_debt.amount += interest_i64;
      int128_t supply_income = interest * (RATE_SCALE - res.reserve_factor) / RATE_SCALE;
      int128_t protocol_income = interest - supply_income;

      res.total_liquidity.amount += static_cast<int64_t>(supply_income);
      res.protocol_reserve.amount += static_cast<int64_t>(protocol_income);
   }

   res.last_updated = now;
   return res;
}

int64_t tyche_market::_calc_borrow_rate(const reserve_state& res, uint64_t util_bps) const {
   if (util_bps <= res.u_opt) {
      // linear from r0 to r_opt
      int128_t slope = (int128_t)(res.r_opt - res.r0) * util_bps / res.u_opt;
      return res.r0 + static_cast<int64_t>(slope);
   }

   // from u_opt to 100%
   uint64_t excess = util_bps > RATE_SCALE ? RATE_SCALE : util_bps;
   excess -= res.u_opt;
   int128_t slope = (int128_t)(res.r_max - res.r_opt) * excess / (RATE_SCALE - res.u_opt);
   return res.r_opt + static_cast<int64_t>(slope);
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
   int64_t ttl_us = static_cast<int64_t>(_gstate.price_ttl_sec) * 1'000'000;
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

void tyche_market::_transfer_from(name token_contract, name from, const asset& quantity, const string& memo) {
   eosio::action(
      permission_level{from, "active"_n},
      token_contract,
      "transfer"_n,
      std::make_tuple(from, get_self(), quantity, memo)).send();
}

void tyche_market::_transfer_out(name token_contract, name to, const asset& quantity, const string& memo) {
   eosio::action(
      permission_level{get_self(), "active"_n},
      token_contract,
      "transfer"_n,
      std::make_tuple(get_self(), to, quantity, memo)).send();
}

global_state tyche_market::_read_state() const {
   global_state state;
   eosio::singleton<"state"_n, global_state> state_sing(get_self(), get_self().value);
   if (state_sing.exists()) {
      state = state_sing.get();
   }
   return state;
}

void tyche_market::_write_state(const global_state& state) {
   eosio::singleton<"state"_n, global_state> state_sing(get_self(), get_self().value);
   state_sing.set(state, get_self());
}

} // namespace tychefi
