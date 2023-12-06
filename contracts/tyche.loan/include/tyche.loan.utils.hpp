#pragma once
//#include <charconv>
#include <string>
#include <algorithm>
#include <iterator>
#include <eosio/eosio.hpp>
#include "safe.hpp"

#include <eosio/asset.hpp>

using namespace std;

inline int64_t calc_coin_amount( const asset &base_quant, const asset &price ) {
    int128_t precision = calc_precision( base_quant.symbol.precision() );
    return mul64( base_quant.amount, price.amount, precision );
}

inline int64_t div_leverage( const asset &base_quant, int64_t leverage ) {
    return div64( base_quant.amount, leverage, 1 );
}

inline asset calc_quote_quant(const asset &base_quant, const asset &price) {
    return asset(calc_coin_amount(base_quant, price), price.symbol);
}