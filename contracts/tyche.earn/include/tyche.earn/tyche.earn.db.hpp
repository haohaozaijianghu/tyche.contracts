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

#define TBL struct [[eosio::table, eosio::contract("tyche.earn")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.earn")]]


struct aplink_farm {
    name contract           = "aplink.farm"_n;
    uint64_t lease_id       = 12;                        
    asset unit_reward       = asset(1, symbol("APL", 4));
};

NTBL("global") global_t {
    name                admin                   = "tyche.admin"_n;
    name                lp_refueler             = "tyche.admin"_n;                          //LP TRUSD系统充入账户
    name                reward_contract         = "tyche.reward"_n;

    extended_symbol     principal_token         = extended_symbol(MUSDT,  MUSDT_BANK);      //代币MUSDT,用户存入的本金
    extended_symbol     lp_token                = extended_symbol(TRUSD,  TRUSD_BANK);      //代币TRUSD
    asset               min_deposit_amount      = asset(10'000000, MUSDT);                  //10 MU 
 
    uint64_t            tyche_farm_ratio        = 10;                                       //每100MUSDT 奖励0.1TYCHE
    uint64_t            tyche_farm_lock_ratio   = 90;                                       //每100MUSDT 锁仓0.9TYCHE
    uint64_t            tyche_reward_pool_code  = 5;

    aplink_farm         apl_farm;
    bool                enabled                 = true; 

    EOSLIB_SERIALIZE( global_t, (admin)(lp_refueler)(reward_contract)
                                (principal_token)(lp_token)(min_deposit_amount)
                                (tyche_farm_ratio)(tyche_farm_lock_ratio)(tyche_reward_pool_code)
                                (apl_farm)(enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

//E.g. MUSDT, MBTC,HSTZ,MUSDT
struct earn_pool_reward_st {                             
    uint64_t        reward_id;                          //increase upon every reward distribution                                 
    asset           total_rewards;                      //总奖励 = unalloted_rewards + unclaimed_rewards + claimed_rewards
    asset           last_rewards;                       //上一次总奖励金额
    asset           unalloted_rewards;                  //未分配的奖励(admin)
    asset           unclaimed_rewards;                  //已分配未领取奖励(customer)
    asset           claimed_rewards;                    //已领取奖励
    int128_t        reward_per_share            = 0;    //每票已分配奖励
    int128_t        last_reward_per_share       = 0;    //奖励发放delta
    time_point_sec  reward_added_at;                    //最近奖励发放时间(admin)
    time_point_sec  prev_reward_added_at;               //前一次奖励发放时间间隔
};
using earn_pool_reward_map = std::map<eosio::symbol, earn_pool_reward_st>;

//Scope: _self
TBL earn_pool_t {
    uint64_t                code;                                           //PK: 1,2,3,4,5
    uint64_t                term_interval_sec       = DAY_SECONDS;          //入池锁仓时长秒: x 1, 30, 90, 180, 360 
    uint64_t                share_multiplier        = 1;

    asset                   cum_principal           = asset(0, MUSDT);      //历史总存款金额（本金）
    asset                   avl_principal           = asset(0, MUSDT);      //剩余存款金额（本金）
    earn_pool_reward_st     interest_reward;                                //利息信息, E.g. 3% APY, triggered
    earn_pool_reward_map    airdrop_rewards;                                //manually airdropped rewards, including MUSDT etc types
    
    bool                    on_shelf                = true;
    time_point_sec          created_at;

    earn_pool_t() {}
    earn_pool_t(const uint64_t& c): code(c) {}
    uint64_t primary_key()const { return code; }

    typedef multi_index<"earnpools"_n, earn_pool_t> tbl_t;

    EOSLIB_SERIALIZE( earn_pool_t,  (code)(term_interval_sec)(share_multiplier)
                                    (cum_principal)(avl_principal)(interest_reward)(airdrop_rewards)
                                    (on_shelf)(created_at) )
};

struct earner_reward_st {
    int128_t            last_reward_per_share       = 0;
    asset               unclaimed_rewards;
    asset               claimed_rewards;            //每个池子，每一个期间领取的奖励
    asset               total_claimed_rewards;          
};

using earner_reward_map = std::map<eosio::symbol/*symbol code*/, earner_reward_st>;

//Scope: code
//Note: record will be deleted upon withdrawal/redemption
TBL earner_t {
    name                owner;                          //PK
    asset               cum_principal;                  //总存款金额
    asset               avl_principal;                  //当前存款金额

    earner_reward_st    interest_reward;                //利息信息
    earner_reward_map   airdrop_rewards;                //每票已分配奖励
   
    time_point_sec      term_started_at;                //利息周期开始时间一旦有钱充入进来，周期从当前时间开始
    time_point_sec      term_ended_at;                  //利息周期结束时间
    time_point_sec      created_at;

    earner_t() {}
    earner_t(const name& a): owner(a) {}

    uint64_t primary_key()const { return owner.value; }

    typedef multi_index<"earners"_n, earner_t> tbl_t;

    EOSLIB_SERIALIZE( earner_t,     (owner)(cum_principal)(avl_principal)
                                    (interest_reward)(airdrop_rewards)
                                    (term_started_at)(term_ended_at)(created_at) )
};

//Scope: _self
TBL reward_symbol_t {
    extended_symbol sym;                                    //PK, sym.code MUSDT,8@amax.mtoken
    bool            on_shelf;

    reward_symbol_t() {}

    uint64_t primary_key() const { return sym.get_symbol().code().raw(); }

    typedef eosio::multi_index< "rewardsymbol"_n, reward_symbol_t > idx_t;

    EOSLIB_SERIALIZE( reward_symbol_t, (sym)(on_shelf) )
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