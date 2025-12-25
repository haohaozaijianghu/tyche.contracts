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

   asset shares = _shares_from_amount(quantity, res.total_supply_shares, res.total_liquidity);

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
      row.last_updated        = res.last_updated;
   });

   _transfer_from(res.token_contract, owner, quantity, "supply");
}

void tyche_market::withdraw(name owner, asset quantity) {
   require_auth(owner);
   check(!_gstate.paused, "market paused");
   check(quantity.amount > 0, "quantity must be positive");

   reserves_t reserves(get_self(), get_self().value);
   auto       res_itr = reserves.find(quantity.symbol.code().raw());
   check(res_itr != reserves.end(), "reserve not found");
   check(!res_itr->paused, "reserve paused");

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

   asset share_delta = _shares_from_amount(quantity, res.total_supply_shares, res.total_liquidity);

   owner_idx.modify(pos_itr, owner, [&](auto& row) {
      row.supply_shares -= share_delta;
   });

   valuation val = _compute_valuation(owner);
   check(val.debt_value == 0 || val.collateral_value >= val.debt_value, "health factor below 1 after withdraw");

   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = res.total_liquidity - quantity;
      row.total_supply_shares = res.total_supply_shares - share_delta;
      row.total_debt          = res.total_debt;
      row.last_updated        = res.last_updated;
   });

   _transfer_out(res.token_contract, owner, quantity, "withdraw");
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

   auto res = _accrue(*res_itr);

   uint64_t available = res.total_liquidity.amount - res.total_debt.amount;
   check(quantity.amount <= (int64_t)available, "insufficient liquidity");

   positions_t positions(get_self(), get_self().value);
   auto        pos_ptr = _get_or_create_position(positions, owner, quantity.symbol.code(), quantity);

   asset borrow_shares = _shares_from_amount(quantity, res.total_borrow_shares, res.total_debt);

   positions.modify(*pos_ptr, same_payer, [&](auto& row) {
      row.borrow_shares += borrow_shares;
   });

   valuation val = _compute_valuation(owner);
   check(val.debt_value > 0, "valuation error");
   check(val.debt_value <= val.max_borrowable_value, "exceeds max LTV");
   check(val.collateral_value >= val.debt_value, "health factor below 1 after borrow");

   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = res.total_liquidity;
      row.total_debt          = res.total_debt + quantity;
      row.total_borrow_shares = res.total_borrow_shares + borrow_shares;
      row.total_supply_shares = res.total_supply_shares;
      row.last_updated        = res.last_updated;
   });

   _transfer_out(res.token_contract, owner, quantity, "borrow");
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

   asset share_delta = _shares_from_amount(repay_amount, res.total_borrow_shares, res.total_debt);

   owner_idx.modify(pos_itr, same_payer, [&](auto& row) {
      row.borrow_shares -= share_delta;
   });

   reserves.modify(res_itr, same_payer, [&](auto& row) {
      row.total_debt          = res.total_debt - repay_amount;
      row.total_borrow_shares = res.total_borrow_shares - share_delta;
      row.total_liquidity     = res.total_liquidity + repay_amount;
      row.total_supply_shares = res.total_supply_shares;
      row.last_updated        = res.last_updated;
   });

   _transfer_from(res.token_contract, payer, repay_amount, "repay");
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

   // cap to 50% of borrower's debt value
   prices_t prices(get_self(), get_self().value);
   auto     debt_price_itr = prices.find(debt_sym.raw());
   auto     col_price_itr  = prices.find(collateral_sym.raw());
   check(debt_price_itr != prices.end() && col_price_itr != prices.end(), "missing price feed");

   int128_t repay_value = (int128_t)repay_amount.amount * debt_price_itr->price / PRICE_SCALE;
   int128_t max_repay   = (int128_t)borrower_debt.amount * debt_price_itr->price / PRICE_SCALE / 2;
   check(repay_value <= max_repay, "repay amount exceeds close factor");

   asset debt_share_delta = _shares_from_amount(repay_amount, debt_res.total_borrow_shares, debt_res.total_debt);

   // collateral to seize with bonus
   int128_t seize_value = repay_value * (collat_res.liquidation_bonus) / RATE_SCALE;
   int64_t  seize_amount = (int64_t)(seize_value * PRICE_SCALE / col_price_itr->price);
   asset    seize_asset  = asset(seize_amount, collat_res.total_liquidity.symbol);

   asset collateral_balance = _amount_from_shares(coll_pos->supply_shares, collat_res.total_supply_shares, collat_res.total_liquidity);
   check(collateral_balance.amount >= seize_asset.amount, "insufficient collateral");

   owner_idx.modify(debt_pos, same_payer, [&](auto& row) {
      row.borrow_shares -= debt_share_delta;
   });

   asset collat_share_delta = _shares_from_amount(seize_asset, collat_res.total_supply_shares, collat_res.total_liquidity);
   owner_idx.modify(coll_pos, same_payer, [&](auto& row) {
      row.supply_shares -= collat_share_delta;
   });

   reserves.modify(debt_itr, same_payer, [&](auto& row) {
      row.total_debt          = debt_res.total_debt - repay_amount;
      row.total_borrow_shares = debt_res.total_borrow_shares - debt_share_delta;
      row.total_liquidity     = debt_res.total_liquidity + repay_amount;
      row.total_supply_shares = debt_res.total_supply_shares;
      row.last_updated        = debt_res.last_updated;
   });

   reserves.modify(collat_itr, same_payer, [&](auto& row) {
      row.total_liquidity     = collat_res.total_liquidity - seize_asset;
      row.total_supply_shares = collat_res.total_supply_shares - collat_share_delta;
      row.total_debt          = collat_res.total_debt;
      row.total_borrow_shares = collat_res.total_borrow_shares;
      row.last_updated        = collat_res.last_updated;
   });

   _transfer_from(debt_res.token_contract, liquidator, repay_amount, "liquidate repay");
   _transfer_out(collat_res.token_contract, liquidator, seize_asset, "liquidate seize");
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
      res.total_liquidity.amount += static_cast<int64_t>(supply_income);
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

asset tyche_market::_shares_from_amount(const asset& amount, const asset& total_shares, const asset& total_amount) const {
   check(amount.symbol == total_amount.symbol, "symbol mismatch");
   if (total_amount.amount == 0 || total_shares.amount == 0) {
      return amount;
   }
   int128_t numerator   = (int128_t)amount.amount * total_shares.amount;
   int64_t  share_value = static_cast<int64_t>(numerator / total_amount.amount);
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
      if (res.last_updated != res_itr->last_updated || res.total_debt != res_itr->total_debt ||
          res.total_liquidity != res_itr->total_liquidity || res.total_borrow_shares != res_itr->total_borrow_shares ||
          res.total_supply_shares != res_itr->total_supply_shares) {
         reserves.modify(res_itr, same_payer, [&](auto& row) { row = res; });
      }

      auto price_itr = prices.find(itr->sym_code.raw());
      check(price_itr != prices.end(), "missing price for valuation");

      int128_t price = price_itr->price;

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

void tyche_market::_check_price_available(symbol_code sym) const {
   prices_t prices(get_self(), get_self().value);
   check(prices.find(sym.raw()) != prices.end(), "price not available");
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
