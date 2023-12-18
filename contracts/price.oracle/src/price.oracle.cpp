#include <price.oracle/price.oracle.hpp>
#include <price.oracle/utils.hpp>

using namespace eosio;
using namespace std;
using namespace orcale;

using std::string;

/**
 * 添加预言人
 */
void price_oracle::addseer(const name& seer) {
    require_auth(get_self());

    auto seer_item = seer_t(seer);
    CHECKC( !_dbc.get( seer_item ), err::NO_AUTH, "seer account is existing");
    _dbc.set( _self.value, seer_item, false);
}

void price_oracle::removeseer(const name& seer) {
    require_auth(get_self());
    auto seer_item = seer_t(seer);
    CHECKC( _dbc.get( seer_item), err::RECORD_NOT_FOUND,  "seer account is invalid" );
    _dbc.del_scope(_self.value, seer_item);
}

void price_oracle::addcoin(const name& coin) {
    require_auth(get_self());

    CHECKC( _gstate.prices.count(coin) == 0, err::RECORD_FOUND, "coin is existing" );
    _gstate.prices[coin] = 0;
}

void price_oracle::removecoin(const name& coin) {
    require_auth(get_self());
    CHECKC( _gstate.prices.count(coin) , err::RECORD_NOT_FOUND, "coin is not found" );
    _gstate.prices.erase(coin);
}

void price_oracle::updateprice(const name& seer, const std::vector<coin_price_info>& infos ) {
    require_auth(seer);
    auto seer_item = seer_t(seer);
    CHECKC( _dbc.get( seer_item ), err::NO_AUTH,  "seer account is invalid" );
    CHECKC( !infos.empty(), err::PARAM_ERROR, "infos length must bigger than 0")
    for(auto& info : infos) {
        _updateprice( info.tpcode, info.price );
    }
}

void price_oracle::_updateprice( const name& tpcode, const asset& price) {
    std::string tpcode_str = tpcode.to_string();
    auto coins = split(tpcode_str, ".");
    CHECKC(name(coins[1])   == _gstate.quote_code, err::RECORD_NOT_FOUND, "coin is not found" );
    CHECKC(price.symbol     == _gstate.quote_symbol, err::RECORD_NOT_FOUND, "coin is not found" );
    auto coin = name(coins[0]);

    CHECKC( _gstate.prices.count(coin), err::RECORD_NOT_FOUND, "coin is not found" );
    auto older_price = _gstate.prices[coin];
    if(older_price != 0) {
        auto upper_limit = multiply_decimal64(older_price, 15, 10);
        auto down_limit = multiply_decimal64(older_price, 5, 10);
        CHECKC(price.amount > down_limit && price.amount < upper_limit, err::PARAM_ERROR, "price not valid" )
    }
    _gstate.prices[coin] = price.amount;

    coin_price_t::idx_t prices(_self, coin.value);
    auto itr = prices.begin();
    auto id = 1;
    if (itr != prices.end()) {
        id = itr->id + 1;
        prices.erase(itr);
    }

    prices.emplace(_self, [&](auto& p) {
        p.id            = id;
        p.price         = price;
        p.tpcode        = tpcode;
        p.updated_at    = current_block_time();
    });
}

