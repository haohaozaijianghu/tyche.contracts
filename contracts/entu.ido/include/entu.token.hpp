#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <cmath>
#include <string>

#define ISSUE(bank, to, quantity, memo) \
    {	entu_token::xtoken::issue_action act{ bank, { {_self, active_perm} } };\
        act.send( to, quantity, memo );}

#define BURN(bank, from, quantity) \
    {	entu_token::xtoken::burn_action act{ bank, { {_self, active_perm} } };\
            act.send( from, quantity, memo );}

#define TRANSFER(bank, to, quantity, memo) \
    {	entu_token::xtoken::transfer_action act{ bank, { {_self, active_perm} } };\
            act.send( _self, to, quantity , memo );}

#define FEE_NOTIFY(from, to, fee_receiver, fee, memo ) \
    {   entu_token::xtoken::notifypayfee_action act{ _self, { {_self, active_perm} } };\
            act.send( from, to, fee_receiver, fee, memo ); }

namespace entu_token
{

    using std::string;
    using namespace eosio;

    static constexpr int16_t RATIO_BOOST        = 1'0000;
    static constexpr int16_t ENTU_PRECISION     = 1'0000;
    static constexpr eosio::symbol ENTU_SYMBOL  = symbol("ENTU", 4);
    static constexpr eosio::name active_perm    {"active"_n};
  
    /**
     * The `entu.token` sample system contract defines the structures and actions that allow users to create, issue, and manage tokens for AMAX based blockchains. It demonstrates one way to implement a smart contract which allows for creation and management of tokens. It is possible for one to create a similar contract which suits different needs. However, it is recommended that if one only needs a token with the below listed actions, that one uses the `entu.token` contract instead of developing their own.
     *
     * The `entu.token` contract class also implements two useful public static methods: `get_supply` and `get_balance`. The first allows one to check the total supply of a specified token, created by an account and the second allows one to check the balance of a token for a specified account (the token creator account has to be specified as well).
     *
     * The `entu.token` contract manages the set of tokens, accounts and their corresponding balances, by using two internal multi-index structures: the `accounts` and `stats`. The `accounts` multi-index table holds, for each row, instances of `account` object and the `account` object holds information about the balance of one token. The `accounts` table is scoped to an eosio account, and it keeps the rows indexed based on the token's symbol.  This means that when one queries the `accounts` multi-index table for an account name the result is all the tokens that account holds at the moment.
     *
     * Similarly, the `stats` multi-index table, holds instances of `currency_stats` objects for each row, which contains information about current supply, maximum supply, and the creator account for a symbol token. The `stats` table is scoped to the token symbol.  Therefore, when one queries the `stats` table for a token symbol the result is one single entry/row corresponding to the queried symbol token if it was previously created, or nothing, otherwise.
     * The `entu.token` is base on `amax.token`, support fee of transfer
     */
    class [[eosio::contract( "entu.token" )]] xtoken : public contract
    {
    public:
        using contract::contract;

        xtoken(eosio::name receiver, eosio::name code, datastream<const char*> ds): 
            contract(receiver, code, ds), _global(_self, _self.value) 
        {
            if (_global.exists()) {
                _g = _global.get();

            } else { // first init
                _g = global_t{};
            }
        }

        ~xtoken() { _global.set( _g, get_self() ); }

        /**
         *  This action issues to `to` account a `quantity` of tokens.
         *
         * @param to - the account to issue tokens to, it must be the same as the issuer,
         * @param quntity - the amount of tokens to be issued,
         * @memo - the memo string that accompanies the token issue transaction.
         */
        ACTION issue(const name &to, const asset &quantity, const string &memo);

        /**
         * The opposite for create action, if all validations succeed,
         * it debits the statstable.supply amount.
         *
         * @param quantity - the quantity of tokens to burn,
         * @param memo - the memo string to accompany the transaction.
         */
        ACTION burn(const asset &quantity, const string &memo);

        /**
         * Allows `from` account to transfer to `to` account the `quantity` tokens.
         * One account is debited and the other is credited with quantity tokens.
         *
         * @param from - the account to transfer from,
         * @param to - the account to be transferred to,
         * @param quantity - the quantity of tokens to be transferred,
         * @param memo - the memo string to accompany the transaction.
         */
        ACTION transfer(const name &from,
                                        const name &to,
                                        const asset &quantity,
                                        const string &memo);

        /**
         * Notify pay fee.
         * Must be Triggered as inline action by transfer()
         *
         * @param from - the from account of transfer(),
         * @param to - the to account of transfer, fee payer,
         * @param fee_receiver - fee receiver,
         * @param fee - the fee of transfer to be payed,
         * @param memo - the memo of the transfer().
         * Require contract auth
         */
        ACTION notifypayfee(const name &from, const name &to, const name& fee_receiver, const asset &fee, const string &memo) {
            require_auth( _self );
            
            require_recipient( to );
            require_recipient( fee_receiver );
        }


        /**
         * Pause token
         * If token is paused, users can not do actions: transfer(), open(), close(),
         * @param symbol - the symbol of the token.
         * @param paused - is paused.
         */
        ACTION pause(const bool& paused) {
            require_auth( _g.admin );
            _g.paused = paused;
        }

        /**
         * freeze account
         * If account of token is frozen, it can not do actions: transfer(), close(),
         * @param symbol - the symbol of the token.
         * @param account - account name.
         * @param is_frozen - is account frozen.
         */
        ACTION freezeacct(const name &account, bool is_frozen) {
            require_auth(_g.issuer);

            accounts accts(get_self(), account.value);
            auto sym_code_raw = _g.supply.symbol.code().raw();
            const auto &acct = accts.get(sym_code_raw, "account of token does not exist");

            accts.modify(acct, _g.issuer, [&](auto &a) {
                a.is_frozen = is_frozen;
            });
        }

        ACTION feeexempt(const name &account, const bool& is_fee_exempt) {
            require_auth(_g.issuer);
            
            feeexempt_tbl accts(_self, _self.value);
            auto itr = accts.find( account.value );
            auto found =  ( itr != accts.end() );

            if (is_fee_exempt) {
                check( !found, "account already in fee exempt list" );
                accts.emplace(_self, [&](auto &a) {
                    a.account = account;
                });

            } else {
                check( found, "account not in exempt list" );
                accts.erase(itr);
            }
        }

        static asset get_balance(const name &token_contract_account, const name &owner, const symbol_code &sym_code)
        {
            accounts accountstable(token_contract_account, owner.value);
            const auto &ac = accountstable.get(sym_code.raw());
            return ac.balance;
        }

        using transfer_action = eosio::action_wrapper<"transfer"_n, &xtoken::transfer>;
        using notifypayfee_action = eosio::action_wrapper<"notifypayfee"_n, &xtoken::notifypayfee>;

        ACTION init(const name& issuer, const symbol& symbol) {
            require_auth( _self );
            check( is_account(issuer), "issuer account does not exist" );

            _g.issuer                  = issuer;
            _g.max_supply              = asset(1'000'000'000'0000, symbol); //1 Billion ENTU
            _g.supply                  = asset(0, symbol);
        }

        ACTION setfeeratio(const name& acct, const uint8_t& fee_ratio) {
            require_auth( _g.admin );

            _g.feeconfs[acct] = fee_ratio;
        }

    private:
      struct [[eosio::table("global"), eosio::contract( "entu.token" )]] global_t {
            name admin                  = "entusysadmin"_n;
            std::map<name, uint16_t> feeconfs = { 
                { "oooo"_n,         100 },
                { "entu.lpshare"_n, 200 },
                { "entu.swap"_n,    200 } };

            asset supply;
            asset max_supply;
            name issuer;
            bool paused = false;

            EOSLIB_SERIALIZE( global_t, (admin)(feeconfs)(supply)(max_supply)(issuer)(paused) )
        };

        typedef eosio::singleton< "global"_n, global_t > global_singleton;

        global_singleton    _global;
        global_t            _g;

    private:
        struct [[eosio::table]] fee_exempt_account 
        {
            name account;

            uint64_t primary_key() const { return account.value; }

            EOSLIB_SERIALIZE( fee_exempt_account, (account) )
        };
        typedef eosio::multi_index<"feeexempts"_n, fee_exempt_account> feeexempt_tbl;

        struct [[eosio::table]] account
        {
            asset balance;
            bool  is_frozen = false;

            uint64_t primary_key() const { return balance.symbol.code().raw(); }

            EOSLIB_SERIALIZE( account, (balance)(is_frozen) )
        };
        typedef eosio::multi_index<"accounts"_n, account> accounts;

    private:
        void sub_balance(const name &owner, const asset &value, bool frozen_check_required = false);
        void add_balance(const name &owner, const asset &value, const name &ram_payer, bool frozen_check_required = false);

        inline bool _is_account_frozen(const name &owner, const account &acct) const {
            return acct.is_frozen && owner != _g.issuer;
        }

        bool _is_fee_exempted( const name& account ) {
            feeexempt_tbl accts(_self, _self.value);
            return( accts.find( account.value ) != accts.end() );
        }

    };

}
