#pragma once

#include <map>
#include <set>
#include <eosio/eosio.hpp>
#include <eosio/name.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include "entu.swap.const.hpp"
#include "entu.swap.db.hpp"
#include "utils.hpp"
#include "entu.utils.hpp"

namespace amax {

    using namespace eosio;

    static constexpr eosio::name active_perm{"active"_n};
    
    namespace sym_pair_status {
        static constexpr eosio::name RUNNING{"running"_n };
        static constexpr eosio::name STOP{"stop"_n };
    }

    //scope: _self
    ENTUSWAP_TABLE symbol_pair_t {
        asset           token_quant;       
        name            token_bank;  
        asset           base_swap_token_amount;     //10000.000000000 AMAX
        asset           last_deal_price;
        bool            enabled;

        uint64_t primary_key() const { return token_quant.symbol.code().raw(); }


        symbol_pair_t() {}
        symbol_pair_t(const symbol& token_symbol, const name& bank) :
                    token_quant(asset(0, token_symbol)),
                    token_bank(bank) {}

        typedef eosio::multi_index< "sympair"_n,  symbol_pair_t > idx_t;

        EOSLIB_SERIALIZE( symbol_pair_t, (token_quant)(token_bank)(base_swap_token_amount)(last_deal_price)(enabled) )
    };

    typedef eosio::multi_index<"sympair"_n, symbol_pair_t > symbol_pair_table;
    
    inline static symbol_pair_table make_sympair_table( const name &self ) {
        return symbol_pair_table( self, self.value/*scope*/ );
    }

}// namespace swap
