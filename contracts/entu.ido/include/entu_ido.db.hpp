#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/privileged.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include "utils.hpp"

using namespace eosio;
using namespace std;
using std::string;

// using namespace wasm;
#define SYMBOL(sym_code, precision) symbol(symbol_code(sym_code), precision)

static constexpr eosio::name active_perm        {"active"_n};
static constexpr symbol SYS_SYMBOL              = SYMBOL("ENTU", 8);
static constexpr symbol USDT_SYMBOL             = SYMBOL("MUSDT", 6);
static constexpr name SYS_BANK                  { "entu.token"_n };
static constexpr name USDT_BANK                 { "amax.mtoken"_n };

namespace wasm { namespace db {

struct [[eosio::table("global"), eosio::contract("entu.ido")]] global_t {
    asset       total_entu_quant    = asset_from_string("6300000.00000000 ENTU");
    asset       remain_entu_quant   = asset_from_string("6300000.00000000 ENTU");
    asset       min_buy_amount      = asset_from_string("0.01000000 ENTU");
    asset       max_buy_amount      = asset_from_string("10000.00000000 ENTU");
    asset       price_step          = asset_from_string("0.010000 MUSDT");
    uint64_t    entu_step           = 100000;
    name admin                      = "entusysadmin"_n;

    EOSLIB_SERIALIZE( global_t, (total_entu_quant)(remain_entu_quant)(min_buy_amount)(price_step)(entu_step)(admin) )

    // //write op
    // template<typename DataStream>
    // friend DataStream& operator << ( DataStream& ds, const global_t& t ) {
    //     return ds   << t.entu_price
    //                 << t.min_buy_amount
    //                 << t.admin;
    // }
    
    // //read op (read as is)
    // template<typename DataStream>
    // friend DataStream& operator >> ( DataStream& ds, global_t& t ) {  
    //     return ds;
    // }

};
typedef eosio::singleton< "global"_n, global_t > global_singleton;


} }