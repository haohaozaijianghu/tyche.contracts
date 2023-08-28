#include "entu.swap.hpp"
#include "entu.swap.db.hpp"
#include "eosio.token.hpp"
#include <eosio/transaction.hpp>
#include <eosio/permission.hpp>
#include "entu.utils.hpp"

using namespace amax;
using namespace eosio;
using namespace std;

void entu_swap::setsympair( const symbol& token_symbol, const name& token_bank, const asset& base_swap_token_amount ) {
    require_auth(_self);
    CHECKC(base_swap_token_amount.symbol == ENTU_SYMBOL ,err::PARAM_ERROR, "base_swap_token_amount symbol error");

    symbol_pair_t sympair(token_symbol, token_bank);
    CHECKC(!_dbc.get( sympair ),err::RECORD_FOUND,"token symbol is existed");
    
    sympair.base_swap_token_amount = base_swap_token_amount;
    sympair.last_deal_price = asset(0, token_symbol);
    sympair.enabled         = false;

    _dbc.set(sympair, get_self());
}

void entu_swap::opensympair(const name& sympair_code, const bool& on_off) {
    require_auth(_self);

    auto sympair_tbl = make_sympair_table(_self);
    auto it = sympair_tbl.find(sympair_code.value);
    CHECKC( it != sympair_tbl.end(),err::RECORD_NOT_FOUND, "sympair not found: " + sympair_code.to_string())
    sympair_tbl.modify(*it, same_payer, [&](auto &row) {
        row.enabled                 = on_off;
    });
}

void entu_swap::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
    if (from == get_self()) { return; }
    CHECKC( to == get_self(),   err::PARAM_ERROR, "Must transfer to this contract")
    CHECKC( quant.amount > 0,   err::PARAM_ERROR, "The quantity must be positive")
    auto bank = get_first_receiver();

    if( quant.symbol != ENTU_SYMBOL ) {
        //get symbol
        _on_receive_token(bank, quant);
        return;
    }

    // CHECKC(bank == AMAX_TOKEN, err::PARAM_ERROR,  "Invalid ENTU  bank: " + bank.to_string() )
    _on_receive_swap_token(from, quant, symbol_from_string(memo));
}

void entu_swap::_on_receive_token(const name& bank, const asset& quant) {
    auto sympair_tbl = make_sympair_table(_self);
    auto it = sympair_tbl.find( quant.symbol.code().raw() );
    CHECKC( it != sympair_tbl.end(),err::RECORD_NOT_FOUND, "sympair not found ")
    // CHECKC( bank == it->token_bank, err::PARAM_ERROR,  "Invalid token bank: " + bank.to_string())
    
    sympair_tbl.modify( *it, same_payer, [&](auto &row ) {
        row.token_quant  += quant;
    });
}

void entu_swap::_on_receive_swap_token(const name& from, const asset& quant, const symbol& symbol_code) {
    auto sympair_tbl = make_sympair_table( _self );
    auto it = sympair_tbl.find( symbol_code.code().raw() );
    CHECKC( it != sympair_tbl.end(), err::RECORD_NOT_FOUND, "sympair not found ")

    auto total_fgt_quant = it->base_swap_token_amount + quant;
    auto price = calc_price_quant(it->token_quant, total_fgt_quant);
    auto customer_recv_quant = calc_coin_quant( quant, price, it->token_quant.symbol );
    TRANSFER(AMAX_TOKEN, BLACK_HOLE_ACCOUNT, quant, "")

    TRANSFER(it->token_bank, from, customer_recv_quant, "")
    sympair_tbl.modify(*it, same_payer, [&](auto &row) {
        row.token_quant     -= customer_recv_quant;
        row.last_deal_price = price;
    });
}
