#include <usdt.interest/usdt.interest.hpp>
#include <usdt_save.hpp>
#include <usdt.save.db.hpp>
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
   if(from == _gstate.usdt_save_contract) return;
   if(from == get_self() || to != get_self()) return;
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );
   CHECKC( from == _gstate.refueler_account, err::ACCOUNT_INVALID, "from must: "+ from.to_string() + ", refueler_account:" + _gstate.refueler_account.to_string())

   auto token_bank = get_first_receiver();
   if(memo == "deposit" && quant.symbol == _gstate.total_interest_quant.symbol) {
      CHECKC(token_bank == MUSDT_BANK, err::CONTRACT_MISMATCH, "bank must amax.mtoken ")
      //用户存入本金
      _gstate.total_interest_quant += quant;
   } else {
      usdt_save::onrewardrefuel_action reward_refuel_act(_gstate.usdt_save_contract, { {get_self(), "active"_n} });
      reward_refuel_act.send(token_bank, quant);
   }
   
   
}

//提出奖励
void usdt_interest::claimreward( const name& to, const name& bank, const asset& total_rewards, const string& memo){
   require_auth(_gstate.usdt_save_contract);
   TRANSFER( bank, to, total_rewards, memo )
}

void usdt_interest::setlinterest(){
   _gstate.instert_allocated_started_at = current_time_point();
   auto interval =  (time_point_sec(current_time_point())- _gstate.instert_allocated_started_at);
   auto days = interval.count() / DAY_SECONDS;
   CHECKC( days > 0, err::TIME_PREMATURE, "time premature" )
   //获取本金总数
   auto confs         = earn_pool_t::tbl_t(_gstate.usdt_save_contract, _gstate.usdt_save_contract.value);
   auto conf_itr        = confs.begin();
   CHECKC( conf_itr != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

   auto total_quant = asset(0, _gstate.total_interest_quant.symbol);
   while( conf_itr != confs.end()) {
      total_quant += conf_itr->available_quant;
      conf_itr++;
   }
   auto total_interest = total_quant * _gstate.annual_interest_rate/ PCT_BOOST / YEAR_DAYS * days;
   _gstate.allocated_interest_quant       += total_interest;
   _gstate.instert_allocated_started_at   = current_time_point();
   usdt_save::onrewardrefuel_action reward_refuel_act(_gstate.usdt_save_contract, { {get_self(), "active"_n} });
   reward_refuel_act.send(MUSDT_BANK, total_interest);
}


}