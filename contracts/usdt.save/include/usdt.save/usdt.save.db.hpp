#pragma once

#include <eosio/asset.hpp>
#include <eosio/privileged.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include <utils.hpp>

// #include <deque>
#include <optional>
#include <string>
#include <map>
#include <set>
#include <type_traits>

namespace amax {

using namespace std;
using namespace eosio;

#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size())

#define TBL struct [[eosio::table, eosio::contract("usdt.save")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("usdt.save")]]

NTBL("global") global_t {
    name                admin           = "armoniaadmin"_n;
    extended_symbol     principal_token;            //E.g. 6,MUSDT@amax.token, can be set differently for diff contract
    extended_symbol     voucher_token;              //E.g. 6,NUSDT@amax.token, can be set differently for diff contract
    asset               mini_deposit_amount;
    uint64_t            interest_rate;              //boost by 10000, ONE YEAR
    boolean             enabled;
    asset               total_deposit_quant;
    asset               total_interest_quant;
    asset               remain_deposit_quant;
    asset               remain_interest_quant;

    EOSLIB_SERIALIZE( global_t, (admin)(principal_token)(voucher_token)
                        (mini_deposit_amount)(interest_rate)(enabled)(total_deposit_quant)(total_interest_quant)
                        (remain_deposit_quant)(remain_interest_quant) )

};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

//Scope: _self
TBL save_conf_t {
    uint64_t            code;                //PK

    time_point_sec      created_at;

    save_conf_t() {}
    save_conf_t(const uint64_t& code) {
        account = a;
    }

    uint64_t primary_key()const { return code; }

    typedef multi_index<"saveaccounts"_n, save_account_t> tbl_t;

    EOSLIB_SERIALIZE( save_account_t,   (account)(deposit_quant)(total_deposit_quant)(total_interest_redemption)
                                        (interest_collected)(created_at)(last_interest_settled_at) )

};



//Scope: _self
//Note: record will be deleted upon withdrawal/redemption
TBL save_account_t {
    name                account;                //PK
    asset               deposit_quant;
    asset               total_deposit_quant;
    asset               total_interest_redemption;
    asset               interest_collected;
    time_point_sec      created_at;
    time_point_sec      last_interest_settled_at;

    save_account_t() {}
    save_account_t(const name& a) {
        account = a;
    }

    uint64_t primary_key()const { return account.raw(); }

    typedef multi_index<"saveaccounts"_n, save_account_t> tbl_t;

    EOSLIB_SERIALIZE( save_account_t,   (account)(deposit_quant)(total_deposit_quant)(total_interest_redemption)
                                        (interest_collected)(created_at)(last_interest_settled_at) )

};

} //namespace amax