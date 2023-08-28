#pragma once
//#include <charconv>
#include <string>
#include <algorithm>
#include <iterator>
#include <eosio/eosio.hpp>
#include "safe.hpp"

#include <eosio/asset.hpp>

using namespace std;

inline string symbol_to_string(const symbol &s) {
    return std::to_string(s.precision()) + "," + s.code().to_string();
}

inline string symbol_pair_to_string(const symbol &asset_symbol, const symbol &coin_symbol) {
    return symbol_to_string(asset_symbol) + "/" + symbol_to_string(coin_symbol);
}

inline uint64_t get_scope(const uint64_t &pair_id, const uint64_t type) {
    return pair_id * 10000 + type;
}


inline int64_t power(int64_t base, int64_t exp) {
    int64_t ret = 1;
    while( exp > 0  ) {
        ret *= base; --exp;
    }
    return ret;
}

inline int64_t power10(int64_t exp) {
    return power(10, exp);
}

inline int64_t calc_precision(int64_t digit) {
    CHECK(digit >= 0 && digit <= 18, "precision digit " + std::to_string(digit) + " should be in range[0,18]");
    return power10(digit);
}

int64_t calc_price_amount(const asset &coin_quant, const asset &asset_quant) {
    int128_t precision = calc_precision(asset_quant.symbol.precision());
    return divide_decimal64(coin_quant.amount, asset_quant.amount, precision);
}

int64_t calc_asset_amount(const asset &coin_quant, const asset &price, const symbol &asset_symbol) {
    ASSERT(coin_quant.symbol.precision() == price.symbol.precision());
    int128_t precision = calc_precision(asset_symbol.precision());
    return divide_decimal64(coin_quant.amount, price.amount, precision);
}

int64_t calc_coin_amount(const asset &asset_quant, const asset &price, const symbol &coin_symbol) {
    ASSERT(coin_symbol.precision() == price.symbol.precision());
    int128_t precision = calc_precision(asset_quant.symbol.precision());
    return multiply_decimal64(asset_quant.amount, price.amount, precision);
}

asset calc_asset_quant(const asset &coin_quant, const asset &price, const symbol &asset_symbol) {
    return asset(calc_asset_amount(coin_quant, price, asset_symbol), asset_symbol);
}

asset calc_coin_quant(const asset &asset_quant, const asset &price, const symbol &coin_symbol) {
    return asset(calc_coin_amount(asset_quant, price, coin_symbol), coin_symbol);
}

asset calc_price_quant(const asset &coin_quant, const asset &asset_quant) {
   
    return asset ( calc_price_amount(coin_quant, asset_quant), coin_quant.symbol );
}



