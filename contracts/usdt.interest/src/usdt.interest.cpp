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
   _gstate.refueler_account      = refueler_account;
   _gstate.usdt_save_contract    = usdt_save_contract;
}

/**
 * @brief send nasset tokens into nftone marketplace
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo:1	interest	打入MUSDT,到期利息	到期前不能领取
               2	reward:(0,1,2,3,4,5)	0：按池子系数瓜分
                                       1-5：MUSDT，AMMX 指定池子空投	打入利息后就可以领取
 *             
 */
//管理员打入奖励
void usdt_interest::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   if(from == _gstate.usdt_save_contract) return;
   if(from == get_self() || to != get_self()) return;
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );
   CHECKC( from == _gstate.refueler_account, err::ACCOUNT_INVALID, "from must: "+ from.to_string() + ", refueler_account:" + _gstate.refueler_account.to_string())

   auto token_bank = get_first_receiver();
   if(memo == "interest" && quant.symbol == _gstate.total_interest_quant.symbol) {
      CHECKC(token_bank == MUSDT_BANK, err::CONTRACT_MISMATCH, "bank must amax.mtoken ")
      //用户存入本金
      _gstate.total_interest_quant += quant;
   } else {
      vector<string_view> memo_params = split(memo, ":"); 
      CHECKC(memo_params.size() == 2, err::PARAM_INVALID, "memo invalid")
      auto action = memo_params[0];
      CHECKC(action == "reward", err::PARAM_INVALID, "memo invalid")
      auto pool_conf_code = std::stoi(string(memo_params[1]));

      _save_reward_info(quant, token_bank, pool_conf_code);

      usdt_save::onrewardrefuel_action reward_refuel_act(_gstate.usdt_save_contract, { {get_self(), "active"_n} });
      reward_refuel_act.send(token_bank, quant, DAY_SECONDS, pool_conf_code);
   }
}

//提出奖励
void usdt_interest::claimreward( const name& to, const name& bank, const asset& total_rewards, const string& memo){
   require_auth(_gstate.usdt_save_contract);
   _sub_reward(total_rewards, bank);
   TRANSFER( bank, to, total_rewards, memo)
}

//提取利息
void usdt_interest::claimintr( const name& to, const name& bank, const asset& total_interest, const string& memo){
   require_auth(_gstate.usdt_save_contract);
   _gstate.redeemed_interest_quant -= total_interest;
   TRANSFER( bank, to, total_interest, memo)
}

void usdt_interest::onpoolstart(){
   require_auth(_self);
   _gstate.instert_allocated_started_at = current_time_point();
}

//触发利息,每天触发一次
void usdt_interest::splitintr(){
   auto interval =  (time_point_sec(current_time_point()) - _gstate.instert_allocated_started_at);
   auto seconds = interval.count()/1000000;
   CHECKC( seconds > 0, err::TIME_PREMATURE, "time premature" )
   //获取本金总数
   auto confs         = earn_pool_t::tbl_t(_gstate.usdt_save_contract, _gstate.usdt_save_contract.value);
   auto conf_itr      = confs.begin();
   CHECKC( conf_itr != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

   auto total_quant = asset(0, _gstate.total_interest_quant.symbol);
   while( conf_itr != confs.end()) {
      total_quant += conf_itr->available_quant;
      conf_itr++;
   }
   auto total_interest = total_quant * _gstate.annual_interest_rate / (YEAR_DAYS* DAY_SECONDS) * seconds /PCT_BOOST;
   CHECKC(total_interest.amount > 10, err::INCORRECT_AMOUNT,  "interet too small: " + total_interest.to_string() )
   _gstate.allocated_interest_quant       += total_interest;
   _gstate.instert_allocated_started_at   = current_time_point();
   usdt_save::onintrrefuel_action interest_refuel_act(_gstate.usdt_save_contract, { {get_self(), "active"_n} });
   interest_refuel_act.send(MUSDT_BANK, total_interest, seconds);
}

//设置年化利率
void usdt_interest::setrate(uint64_t& rate){
   require_auth(_self);
   _gstate.annual_interest_rate = rate;
}

void usdt_interest::_save_reward_info(const asset& quant, const name& token_bank, const uint64_t& pool_conf_code){
   auto rewards = reward_t::tbl_t(_self, _self.value);
   auto reward_itr = rewards.find(quant.symbol.code().raw());
   if(reward_itr == rewards.end()) {
      rewards.emplace(_self, [&](auto& row) {
         row.total_reward_quant = quant;
         row.bank = token_bank;
         row.memo = "reward: " + to_string(pool_conf_code);
         row.last_reward_at = current_time_point();
         row.updated_at = current_time_point();
      });
   } else {
      CHECKC(reward_itr->bank == token_bank, err::CONTRACT_MISMATCH, "bank mismatch")
      rewards.modify(reward_itr, _self, [&](auto& row) {
         row.total_reward_quant += quant;
         row.memo = "reward: " + to_string(pool_conf_code);
         row.updated_at = current_time_point();
      });
   }
}

void usdt_interest::_sub_reward(const asset& quant, const name& token_bank){
   auto rewards = reward_t::tbl_t(_self, _self.value);
   auto reward_itr = rewards.find(quant.symbol.code().raw());
   CHECKC(reward_itr != rewards.end(), err::RECORD_NOT_FOUND, "reward not found")
   CHECKC(reward_itr->bank == token_bank, err::CONTRACT_MISMATCH, "bank mismatch")
   CHECKC(reward_itr->total_reward_quant >= quant, err::INCORRECT_AMOUNT, "reward not enough")
   rewards.modify(reward_itr, _self, [&](auto& row) {
      row.total_reward_quant -= quant;
      row.updated_at = current_time_point();
   });
}  

}