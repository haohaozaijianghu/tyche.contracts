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

namespace tychefi {

using namespace std;
using namespace eosio;


static constexpr uint16_t  PCT_BOOST         = 10000;
static constexpr uint64_t  DAY_SECONDS       = 24 * 60 * 60;
static constexpr uint64_t  YEAR_SECONDS      = 24 * 60 * 60 * 365;
static constexpr uint64_t  YEAR_DAYS         = 365;
static constexpr int128_t  HIGH_PRECISION    = 1'000'000'000'000'000'000; // 10^18

static constexpr name       MUSDT_BANK       = "amax.mtoken"_n;
static constexpr symbol     MUSDT            = symbol(symbol_code("MUSDT"), 6);
static constexpr symbol     FFT            = symbol(symbol_code("FFT"), 4);
static constexpr name       TRUSD_BANK       = "tyche.token"_n;
static constexpr symbol     TRUSD            = symbol(symbol_code("TRUSD"), 6);
static constexpr name       TYCHE_BANK       = "tyche.token"_n;
static constexpr symbol     TYCHE            = symbol(symbol_code("TYCHE"), 8);
static constexpr name       APLINK_BANK      = "aplink.token"_n ;
static constexpr symbol     APLINK_SYMBOL    = symbol(symbol_code("APL"), 4);
// static constexpr name       INTEREST         = "interest"_n ;
// static constexpr name       REDPACK          = "redpack"_n ;


#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size())

#define TBL struct [[eosio::table, eosio::contract("tyche.swap")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.swap")]]

struct split_parm_t {
    name        owner;
    name        bank;
    asset       quant;
    string      memo;
    };

NTBL("global") global_t {
    name                admin                   = "tyche.admin"_n;
    asset               lower_price             = asset(1, MUSDT);
    asset               total_fft_quant         = asset(0, FFT);

    bool                enabled                 = true; 

    EOSLIB_SERIALIZE( global_t, (admin)(lower_price)(total_fft_quant)(enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

TBL globalidx {
    uint64_t        order_id                   = 0;               // the auto-increament reward id
};
typedef eosio::singleton< "globalidx"_n, globalidx > global_table;

struct global_state: public globalidx {
    public:
        bool changed = false;

        using ptr_t = std::unique_ptr<global_state>;

        static ptr_t make_global(const name &contract) {
            std::shared_ptr<global_table> global_tbl;
            auto ret = std::make_unique<global_state>();
            ret->_global_tbl = std::make_unique<global_table>(contract, contract.value);

            if (ret->_global_tbl->exists()) {
                static_cast<globalidx&>(*ret) = ret->_global_tbl->get();
            }
            return ret;
        }

        inline uint64_t new_auto_inc_id(uint64_t &id) {
            if (id == 0 || id == std::numeric_limits<uint64_t>::max()) {
                id = 1;
            } else {
                id++;
            }
            change();
            return id;
        }

        inline uint64_t new_order_id() {
            return new_auto_inc_id(order_id);
        }
        inline void change() {
            changed = true;
        }

        inline void save(const name &payer) {
            if (changed) {
                auto &g = static_cast<globalidx&>(*this);
                _global_tbl->set(g, payer);
                changed = false;
            }
        }
    private:
        std::unique_ptr<global_table> _global_tbl;  
};

} //namespace tychefi