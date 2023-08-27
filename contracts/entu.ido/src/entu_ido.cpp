
#include <entu.token.hpp>
#include "entu_ido.hpp"
#include "utils.hpp"
#include "math.hpp"

#include <chrono>

using std::chrono::system_clock;
using namespace wasm;

inline int64_t get_precision(const symbol &s) {
    int64_t digit = s.precision();
    CHECK(digit >= 0 && digit <= 18, "precision digit " + std::to_string(digit) + " should be in range[0,18]");
    return calc_precision(digit);
}

inline int64_t get_precision(const asset &a) {
    return get_precision(a.symbol);
}

[[eosio::action]]
void entu_ido::init( const name& admin ) {
  require_auth( _self );

  _gstate.admin = admin;
}

void entu_ido::ontransfer(name from, name to, asset quantity, string memo) {
    if (from == get_self() || to != get_self()) return;

    auto first_contract = get_first_receiver();
    if (first_contract == SYS_BANK) return; //refuel only

	CHECK( quantity.amount > 0, "quantity must be positive" )
    CHECK( first_contract == USDT_BANK, "None USDT contract not allowed: " + first_contract.to_string() )
    CHECK( quantity.symbol == USDT_SYMBOL, "None USDT symbol not allowed: " + quantity.to_string() )

    auto entu_price_step1               = get_current_entu_price();
    auto curr_level_remain_entu_quant   = get_current_level_entu_quant();
    auto recv_entu_quant                = calc_asset_quant(quantity, entu_price_step1, SYS_SYMBOL);
    auto total_entu_quant               = recv_entu_quant;
    if( curr_level_remain_entu_quant < recv_entu_quant ){
        auto remain_usdt_quant = quantity - calc_coin_quant(curr_level_remain_entu_quant, entu_price_step1);
        auto recv_entu_step2     = calc_asset_quant(remain_usdt_quant, entu_price_step1 + _gstate.price_step, SYS_SYMBOL );
        total_entu_quant    = curr_level_remain_entu_quant + recv_entu_step2;
    } 
    CHECK( total_entu_quant < _gstate.remain_entu_quant, "insufficent funds to buy" )
    CHECK( total_entu_quant >= _gstate.min_buy_amount, "buy amount too small: " + total_entu_quant.to_string() )
    CHECK( total_entu_quant <= _gstate.max_buy_amount, "buy amount too big: " + total_entu_quant.to_string() )
    
    TRANSFER( SYS_BANK, from, total_entu_quant, "ido price: " + entu_price_step1.to_string() )
}

asset entu_ido::get_current_entu_price() {
    auto entu_quant = _gstate.total_entu_quant - _gstate.remain_entu_quant;
    auto multi = (entu_quant.amount / (_gstate.entu_step/10)/ get_precision(entu_quant) + 10)/10;

    return asset(_gstate.price_step.amount * multi, _gstate.price_step.symbol);
}

asset entu_ido::get_current_level_entu_quant() {
    auto entu_amount = (_gstate.remain_entu_quant.amount  / get_precision(_gstate.remain_entu_quant.symbol))% _gstate.entu_step;
    return asset(entu_amount, _gstate.remain_entu_quant.symbol);
}


