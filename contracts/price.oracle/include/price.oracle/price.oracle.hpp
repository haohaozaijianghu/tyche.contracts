/*
 * @Author: your name
 * @Date: 2022-04-13 15:58:25
 * @LastEditTime: 2022-04-14 15:40:31
 * @LastEditors: Please set LastEditors
 * @Description: 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 * @FilePath: /deoracle.contracts/contracts/price.oracle/include/price.oracle/price.oracle.hpp
 */
#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/transaction.hpp>
#include <eosio/system.hpp>
#include <eosio/crypto.hpp>
#include <eosio/action.hpp>
#include <string>

#include "wasm_db.hpp"
#include "price.oracle.states.hpp"

using namespace wasm::db;

namespace orcale {

using eosio::asset;
using eosio::check;
using eosio::datastream;
using eosio::name;
using eosio::symbol;
using eosio::symbol_code;
using eosio::unsigned_int;

using std::string;

class [[eosio::contract("price.oracle")]] price_oracle : public eosio::contract {
private:
    global_singleton    _global;
    global_t            _gstate;
    dbc                 _dbc;
    

public:
    using contract::contract;
    price_oracle(eosio::name receiver, eosio::name code, datastream<const char*> ds):
        _dbc(_self),
        contract(receiver, code, ds), _global(_self, _self.value) {
        _gstate = _global.exists() ? _global.get() : global_t{};
    }

    ~price_oracle() {
        _global.set( _gstate, get_self() );
    }

    /**
     * reset the global with default values
     * only code maintainer can init
     */
    [[eosio::action]] 
    void addseer( const name& seer);

    [[eosio::action]] 
    void removeseer( const name& seer);

    [[eosio::action]] 
    void addcoin(const name& coin);

    [[eosio::action]] 
    void removecoin(const name& coin);

    [[eosio::action]] 
    void updateprice(const name& seer, const std::vector<coin_price_info>& infos );

private: 
    void _updateprice( const name& tpcode, const asset& price);

};

}
