#include <entu.token/entu.token.hpp>
using namespace std;

namespace entu_token {

#ifndef ASSERT
    #define ASSERT(exp) eosio::check(exp, #exp)
#endif

#define CHECK(exp, msg) { if (!(exp)) eosio::check(false, msg); }


template<typename Int, typename LargerInt>
LargerInt multiply_decimal(LargerInt a, LargerInt b, LargerInt precision) {
    LargerInt tmp = a * b / precision;
    CHECK(tmp >= std::numeric_limits<Int>::min() && tmp <= std::numeric_limits<Int>::max(),
        "overflow exception of multiply_decimal");
    return tmp;
}

#define mul64(a, b, precision) multiply_decimal<int64_t, int128_t>(a, b, precision)

void xtoken::issue(const name &issuer, const asset &quantity, const string &memo)
{
    require_auth(issuer);

    check(issuer == _g.issuer, "non-issuer auth failure");
    check(memo.size() <= 256, "memo has more than 256 bytes");
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must issue positive quantity");
    check(quantity.symbol == _g.max_supply.symbol, "symbol precision mismatch");
    check(quantity.amount <= _g.max_supply.amount - _g.supply.amount, "quantity exceeds available supply");

    _g.supply += quantity;

    add_balance(_g.issuer, quantity, _g.issuer);
}

void xtoken::burn(const asset &quantity, const string &memo)
{
    require_auth(_g.issuer);

    const auto& sym = quantity.symbol;
    auto sym_code_raw = sym.code().raw();
    check(sym.is_valid(), "invalid symbol name");
    check(memo.size() <= 256, "memo has more than 256 bytes");
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must burn positive quantity");
    check(quantity.symbol == _g.supply.symbol, "symbol mismatch");
    check(_g.supply >= quantity, "supply over-burnt");

    _g.supply -= quantity;

    sub_balance(_g.issuer, quantity);
}

void xtoken::transfer(const name &from, const name &to, const asset &quantity, const string &memo)
{
    require_auth(from);

    check( from != to, "cannot transfer to self" );
    check( is_account(to), "to account does not exist" );
    check( _g.supply.symbol == quantity.symbol, "symbol mismatch" );
    check( !_g.paused, "token transfer paused" );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must transfer positive quantity" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    require_recipient( from );
    require_recipient( to );

    auto payer          = has_auth(to) ? to : from;
    auto actual_recv    = quantity;

    if( to != _g.issuer && !_is_fee_exempted(to) ) {
        for( auto& fee_conf : _g.feeconfs ) {
            auto fee_receiver = fee_conf.first;
            auto fee_ratio = fee_conf.second;
            if( fee_ratio == 0 || fee_receiver == to ) continue;

            auto fee    = quantity * fee_ratio / RATIO_BOOST;
            actual_recv -= fee;

            add_balance( fee_receiver, fee, payer );
            FEE_NOTIFY( from, to, fee_receiver, fee, "" )
        }
    }

    sub_balance(from, quantity, true);
    add_balance(to, actual_recv, payer, true);

}

void xtoken::sub_balance(const name &owner, const asset &quant, bool frozen_check_required)
{
    accounts from_accts( get_self(), owner.value );
    const auto &from = from_accts.get( quant.symbol.code().raw(), "no balance object found" );
    check( !frozen_check_required || !_is_account_frozen(owner, from), "from account is frozen" );
    check( from.balance >= quant, "overdrawn balance" );

    from_accts.modify(from, owner, [&](auto &a) {
        a.balance -= quant;
    });
}

void xtoken::add_balance(const name &owner, const asset &quant, const name &ram_payer, bool frozen_check_required)
{
    accounts to_accts( get_self(), owner.value );
    auto to = to_accts.find( quant.symbol.code().raw() );
    if (to == to_accts.end()) {
        to_accts.emplace(ram_payer, [&](auto &a) {
            a.balance = quant;
        });
        return;
    }

    check( !frozen_check_required || !_is_account_frozen(owner, *to), "to account is frozen" );

    to_accts.modify(to, same_payer, [&](auto &a) {
        a.balance += quant;
    });
}

} /// namespace entu_xtoken
