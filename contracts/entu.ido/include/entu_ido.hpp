#include "entu_ido.db.hpp"

using namespace std;
using namespace wasm::db;

class [[eosio::contract("entu.ido")]] entu_ido: public eosio::contract {
private:
    global_singleton    _global;
    global_t            _gstate;

public:
    using contract::contract;

    entu_ido(eosio::name receiver, eosio::name code, datastream<const char*> ds):
        contract(receiver, code, ds),
        _global(get_self(), get_self().value)
    {
        _gstate = _global.exists() ? _global.get() : global_t{};
    }

    ~entu_ido() { _global.set( _gstate, get_self() ); }
    
    [[eosio::action]] void init(const name& admin);

    // [[eosio::action]] void takeorder();
    /**
     * ontransfer, trigger by recipient of transfer()
     *  @param from - issuer
     *  @param to   - must be contract self
     *  @param quantity - issued quantity
     *  @param memo - memo format:
     */
    [[eosio::on_notify("*::transfer")]] void ontransfer(name from, name to, asset quantity, string memo);

private:
    asset get_current_entu_price();
    asset get_current_level_entu_quant();
}; //contract entu.ido