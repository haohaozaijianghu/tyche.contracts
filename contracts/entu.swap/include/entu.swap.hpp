#pragma once

#include <eosio/eosio.hpp>

#include "entu.swap.const.hpp"
#include "entu.swap.db.hpp"
#include "wasm_db.hpp"

using namespace std;
using namespace eosio;
using namespace wasm::db;

namespace amax {


class [[eosio::contract("entu.swap")]] entu_swap : public contract {
public:
    using contract::contract;

public:
    entu_swap(name receiver, name code, datastream<const char *> ds)
        : contract(receiver, code, ds), 
        _dbc(_self) {}
    
    //AMAX->ENTU
    ACTION setsympair( const symbol& token_symbol, const name& token_bank, const asset& base_swap_token_amount );

    ACTION opensympair( const name& sympair_code, const bool& on_off );

    [[eosio::on_notify("*::transfer")]] 
    void ontransfer( const name& from, const name& to, const asset& quant, const string& memo );

private:
    dbc                     _dbc;

    void _on_receive_swap_token( const name& from, const asset& quant, const symbol& code );

    void _on_receive_token( const name& bank, const asset& quant );

};

}//namespace amax