#include <usdt.interest/usdt.interest.hpp>
#include <usdt_save.hpp>
#include "safemath.hpp"
#include <utils.hpp>
#include <amax.token.hpp>

static constexpr eosio::name active_permission{"active"_n};

namespace amax {
using namespace std;
using namespace wasm::safemath;

#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + to_string((int)code) + string("]] ") + msg); }

void usdt_interest::init(const name& refueler_account, const name& usdt_save_contract, const bool& enabled) {
   require_auth( _self );
   _gstate.enabled               = enabled;
   _gstate.refueler_account        = refueler_account;
   _gstate.usdt_save_contract    = usdt_save_contract;
}

/**
 * @brief send nasset tokens into nftone marketplace
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo
 */
//管理员打入奖励
void usdt_interest::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );
   CHECKC( from == _gstate.refueler_account, err::ACCOUNT_INVALID, "from must: " + _gstate.refueler_account.to_string())
   if (from == get_self() || to != get_self()) return;
   auto token_bank = get_first_receiver();
   usdt_save::onrewardrefuel_action reward_refuel_act(_gstate.usdt_save_contract, { {get_self(), "active"_n} });
   reward_refuel_act.send(token_bank, quant);
}

//提出奖励
void usdt_interest::claimreward( const name& to, const name& bank, const asset& total_rewards, const string& memo){
   require_auth(_gstate.usdt_save_contract);
   TRANSFER( bank, to, total_rewards, memo )
}

}