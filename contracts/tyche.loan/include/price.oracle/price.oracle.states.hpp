 #pragma once

#include <eosio/asset.hpp>
#include <eosio/privileged.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>
#include <eosio/name.hpp>

#include <optional>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <type_traits>




namespace tychefi {

using namespace std;
using namespace eosio;

#define CONTRACT_TBL [[eosio::table, eosio::contract("price.oracle")]]

#define SYMBOL(sym_code, precision) symbol(symbol_code(sym_code), precision)

struct price_global_t {
    name                    version             = "1.o.o"_n;
    map<name, uint64_t>     prices              = {};
    uint64_t                price_history_count = 10;
    name                    quote_code          = "usdt"_n;
    symbol                  quote_symbol        = SYMBOL("USDT", 4);

    price_global_t() {}
    EOSLIB_SERIALIZE(price_global_t, (version)(prices)(price_history_count)(quote_code)(quote_symbol))

    typedef eosio::singleton< "global"_n, price_global_t > idx_t;
};

//scope: btc, eth
 struct [[eosio::table, eosio::contract("price.oracle")]]  seer_t {
    name            seer;
    uint64_t primary_key() const { return seer.value; }

    seer_t() {}
    seer_t(name s): seer(s) {}

    typedef eosio::multi_index< "seers"_n, seer_t > idx_t;
    EOSLIB_SERIALIZE(seer_t, (seer))
};

//scope: btc, eth
 struct [[eosio::table, eosio::contract("price.oracle")]] coin_price_t {
    uint64_t        id;         //auto increment
    name            tpcode;
    asset           price;
    time_point      updated_at;

    uint64_t primary_key() const { return id; }
    uint64_t by_time() const { return updated_at.sec_since_epoch(); }

    coin_price_t() {}
    coin_price_t(uint64_t i): id(i) {}

    typedef eosio::multi_index< "prices"_n, coin_price_t,
        indexed_by<"bytime"_n, const_mem_fun<coin_price_t, uint64_t, &coin_price_t::by_time>>
    > idx_t;

    EOSLIB_SERIALIZE(coin_price_t, (id)(tpcode)(price)(updated_at))
};

} // namespace tychefi