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
static constexpr name       TRUSD_BANK       = "tyche.token"_n;
static constexpr symbol     TRUSD            = symbol(symbol_code("TRUSD"), 6);
static constexpr name       TYCHE_BANK       = "tyche.token"_n;
static constexpr symbol     TYCHE            = symbol(symbol_code("TYCHE"), 8);
static constexpr name       APLINK_BANK      = "aplink.token"_n ;
static constexpr symbol     APLINK_SYMBOL    = symbol(symbol_code("APL"), 4);
// static constexpr name       INTEREST         = "interest"_n ;
// static constexpr name       REDPACK          = "redpack"_n ;


#define HASH256(str) sha256(const_cast<char*>(str.c_str()), str.size())

#define TBL struct [[eosio::table, eosio::contract("tyche.stake")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.stake")]]


struct aplink_farm {
    name contract           = "aplink.farm"_n;
    uint64_t lease_id       = 12;                        
    asset unit_reward       = asset(1, symbol("APL", 4));
};

NTBL("global") global_t {
    name                admin                   = "tyche.admin"_n;
    name                lp_refueler             = "tyche.admin"_n;                          //LP TRUSD系统充入账户
    name                reward_contract         = "tyche.reward"_n;

    extended_symbol     principal_token         = extended_symbol(TYCHE,  TYCHE_BANK);      //代币MUSDT,用户存入的本金
    extended_symbol     lp_token                = extended_symbol(TRUSD,  TRUSD_BANK);      //代币TRUSD
    asset               min_deposit_amount      = asset(10'000000, TYCHE);                  //10 MU 
 
    asset               total_supply            = asset(0, TYCHE);                          //总发行量
    uint64_t            point_history_epoch     = 0;                                        //point_history  epoch
    bool                enabled                 = true;                                     //是否启用
    uint8_t             transfers_enabled       = 1;                                     //是否允许转账

    std::map<uint64_t, int64_t> slope_changes;                                              //# time -> signed slope change


    EOSLIB_SERIALIZE( global_t, (admin)(lp_refueler)(reward_contract)
                                (principal_token)(lp_token)(min_deposit_amount)
                                (total_supply)(point_history_epoch)(enabled)(transfers_enabled)(slope_changes))
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;


//scope: _self
struct earn_stake_locked {
    name                owner;                          //用户  PK
    asset               amount;                         //锁仓金额
    time_point_sec      unlocked_at;                    //解锁时间

    uint64_t primary_key() const { return owner.value; }
    uint64_t by_unlocked_at() const { return unlocked_at.sec_since_epoch(); }

    typedef eosio::multi_index< "earnslocked"_n, earn_stake_locked,
        indexed_by< "unlockedat"_n, const_mem_fun<earn_stake_locked, uint64_t, &earn_stake_locked::by_unlocked_at> >
    > tbl_t;

    EOSLIB_SERIALIZE( earn_stake_locked, (owner)(amount)(unlocked_at) )
};

//Scope: account
TBL user_point_history_t {
    uint64_t                epoch;                             //PK  
    name                    earner;                            //PK: 1,2,3,4,5
    uint64_t                bias;   
    uint64_t                slope;
    uint64_t                block_time;
    uint64_t                block_num;

    user_point_history_t() {}
    user_point_history_t(const uint64_t& c): epoch(c) {}
    uint64_t primary_key()const { return epoch; }

    typedef multi_index<"olduserpoint"_n, user_point_history_t> tbl_t;

    EOSLIB_SERIALIZE( user_point_history_t,  (epoch)(earner)(bias)(slope)(block_time)(block_num) )
};
//scope: _self
TBL user_point_epoch_t {
    name                earner;
    uint64_t            epoch;

    uint64_t primary_key() const { return earner.value; }
    user_point_epoch_t() {}
    user_point_epoch_t(const name& a): earner(a) {}

    typedef eosio::multi_index< "upointepoch"_n, user_point_epoch_t > tbl_t;

    EOSLIB_SERIALIZE( user_point_epoch_t, (earner)(epoch) )
};

//Scope: _self
TBL point_history_t {
    uint64_t                epoch;            //global epoch
    uint64_t                bias;   
    uint64_t                slope;
    uint64_t                block_time;
    uint64_t                block_num;

    point_history_t() {}
    point_history_t(const uint64_t& c): epoch(c) {}
    uint64_t primary_key()const { return epoch; }

    typedef multi_index<"pointhistory"_n, point_history_t> tbl_t;

    EOSLIB_SERIALIZE( point_history_t,  (epoch)(bias)(slope)(block_time)(block_num) )
};

TBL globalidx {
    uint64_t        reward_id                   = 0;               // the auto-increament reward id
    uint64_t        deposit_id                  = 0;               // 本金提取后再存入，id变化
    uint64_t        interest_withdraw_id        = 0;               // the auto-increament id of deal item
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

        inline uint64_t new_reward_id() {
            return new_auto_inc_id(reward_id);
        }

        inline uint64_t new_deposit_id() {
            return new_auto_inc_id(deposit_id);
        }
        inline uint64_t new_interest_withdraw_id() {
            return new_auto_inc_id(interest_withdraw_id);
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