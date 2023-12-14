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
#include <price.oracle/price.oracle.states.hpp>

namespace tychefi {

using namespace std;
using namespace eosio;



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

#define TBL struct [[eosio::table, eosio::contract("tyche.loan")]]
#define NTBL(name) struct [[eosio::table(name), eosio::contract("tyche.loan")]]

struct aplink_farm {
    name contract           = "aplink.farm"_n;
    uint64_t lease_id       = 12;                        
    asset unit_reward       = asset(1, symbol("APL", 4));
};

NTBL("global") global_t {
    name                admin                   = "tyche.admin"_n;
    name                lp_refueler             = "tyche.admin"_n;                          //LP TRUSD系统充入账户
    name                price_oracle_contract   = "price.oracle"_n;

    extended_symbol     loan_token             = extended_symbol(MUSDT,  MUSDT_BANK);       //代币TRUSD
    asset               min_deposit_amount      = asset(10'000000, MUSDT);                  //10 MU
    uint64_t            interest_ratio          = 800;                                      //8%
    uint64_t            term_interval_days      = 365 * 2;                                  //30天

    aplink_farm         apl_farm;

    uint64_t            liquidation_penalty_ratio   = 9000;             //清算惩罚率: 10% = 1000
    uint64_t            liquidation_price_ratio     = 9700 ;            //清算价格 97%

    asset               total_principal_quant;                         //总本金
    bool                enabled                 = true; 

    EOSLIB_SERIALIZE( global_t, (admin)(lp_refueler)(price_oracle_contract)
                                (loan_token)(min_deposit_amount)
                                (interest_ratio)(term_interval_days)(apl_farm)
                                (liquidation_penalty_ratio)(liquidation_price_ratio)(total_principal_quant)
                                (enabled) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

TBL interest_t {
    time_point_sec      begin_at;
    time_point_sec      ended_at;
    uint64_t            interest_ratio;
    uint64_t primary_key()const { return UINT64_MAX - (uint64_t)begin_at.utc_seconds; }

    typedef multi_index<"interests"_n, interest_t> tbl_t;

    EOSLIB_SERIALIZE( interest_t, (begin_at)(ended_at)(interest_ratio) )
};


//Scope: symbol
//Note: record will be deleted upon withdrawal/redemption
TBL loaner_t {
    name                owner;                          //PK
    asset               cum_collateral_quant;           //总存款金额 ETH
    asset               avl_collateral_quant;           //当前存款金额 ETH
    asset               avl_principal;                  //当前存款金额 MUSDT
    uint64_t            interest_ratio;                 //利息率

    time_point_sec      term_started_at;                //入池时间
    time_point_sec      term_settled_at;                //利息结算时间
    time_point_sec      term_ended_at;                  //还款最后时间

    asset               unpaid_interest;                //未支付利息
    asset               paid_interest;                  //已支付利息

    time_point_sec      created_at;

    loaner_t() {}
    loaner_t(const name& a): owner(a) {}

    uint64_t primary_key()const { return owner.value; }

    typedef multi_index<"loaners"_n, loaner_t> tbl_t;

    EOSLIB_SERIALIZE( loaner_t, (owner)(cum_collateral_quant)(avl_collateral_quant)(avl_principal)(interest_ratio)
                                (term_started_at)(term_settled_at)(term_ended_at)
                                (unpaid_interest)(paid_interest)(created_at) )
};

//Scope: _self
TBL collateral_symbol_t {
    extended_symbol sym;                                    //PK, sym.code MUSDT,8@amax.mtoken
    name        oracle_sym_name;                            //价格预言机
    uint64_t    init_collateral_ratio       = 20000;        //初始抵押率 200%
    uint64_t    liquidation_ratio           = 15000;        //抵押率: 150%
    uint64_t    force_liquidate_ratio       = 12000;        //率: 120%
    uint64_t    interest_ratio              = 800;           //利息率: 8% = 800

    asset       total_collateral_quant;                     //抵押物总量    
    asset       avl_collateral_quant;                       //可用抵押物总量
    asset       total_principal;                            //总本金
    asset       avl_principal;                              //可用本金

    asset       total_fore_collateral_quant;                 //强平抵押物总量
    asset       total_fore_principal;                        //强平总本金
    asset       avl_force_collateral_quant;                  //强平抵押物总量
    asset       avl_force_principal;                         //强平需要总本金

    bool        on_shelf;

    collateral_symbol_t() {}

    uint64_t primary_key() const { return sym.get_symbol().code().raw(); }

    typedef eosio::multi_index< "collsyms"_n, collateral_symbol_t > idx_t;

    EOSLIB_SERIALIZE( collateral_symbol_t, (sym)(oracle_sym_name)(init_collateral_ratio)(liquidation_ratio)(force_liquidate_ratio)
                                            (interest_ratio)(total_collateral_quant)(avl_collateral_quant)(total_principal)(avl_principal)
                                            (total_fore_collateral_quant)(total_fore_principal)
                                            (avl_force_collateral_quant)(avl_force_principal)(on_shelf) )
};

TBL fee_pool_t {
    asset fees;      //PK

    uint64_t primary_key() const { return fees.symbol.code().raw(); }

    fee_pool_t() {}

    EOSLIB_SERIALIZE(fee_pool_t, (fees))

};

typedef eosio::multi_index<"feepool"_n, fee_pool_t> feepool_tbl;
inline static feepool_tbl make_fee_table( const name& self ) { return feepool_tbl(self, self.value); }

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