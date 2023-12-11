#pragma once
//#include <charconv>
#include <string>
#include <algorithm>
#include <iterator>
#include <eosio/eosio.hpp>
#include "safe.hpp"

#include <eosio/asset.hpp>
#include<tyche.loan.const.hpp>

using namespace std;


inline int64_t calc_coin_amount( const asset &base_quant, const asset &price ) {
    int128_t precision = calc_precision( base_quant.symbol.precision() );
    return mul64( base_quant.amount, price.amount, precision );
}

inline asset calc_quote_quant(const asset &base_quant, const asset &price) {
    return asset(calc_coin_amount(base_quant, price), price.symbol);
}

inline asset calc_quant( const asset &quant, const uint64_t& ratio ) {
    return asset( mul64(quant.amount, ratio, PCT_BOOST), quant.symbol);
}
inline int64_t calc_asset_amount(const asset &quote_quant, const asset &price, const symbol &base_symbol) {
    ASSERT(quote_quant.symbol.precision() == price.symbol.precision());
    int128_t precision = calc_precision(base_symbol.precision());
    return div64(quote_quant.amount, price.amount, precision);
}
asset calc_base_quant(const asset &quote_quant, const asset &price, const symbol &base_symbol) {
    return asset(calc_asset_amount(quote_quant, price, base_symbol), base_symbol);
}


