#include <tyche.earn/tyche.earn.hpp>
#include <tyche.earn/tyche.earn.db.hpp>
#include <tyche.reward/tyche.reward.hpp>
#include <amax.token.hpp>

#include "safemath.hpp"
#include <utils.hpp>


static constexpr eosio::name active_permission{"active"_n};

namespace tychefi {

using namespace std;
using namespace wasm::safemath;

#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + to_string((int)code) + string("]] ") + msg); }

void tyche_reward::init(const name& refueler_account, const name& tyche_earn_contract, const bool& enabled) {
   require_auth( _self );
   _gstate.enabled                        = enabled;
   _gstate.refueler_account               = refueler_account;
   _gstate.tyche_earn_contract            = tyche_earn_contract;
   _gstate.interest_splitted_at           = current_time_point();
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
void tyche_reward::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   if(from == _gstate.tyche_earn_contract) return;
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

      tyche_earn::onrefuelreward_action reward_refuel_act(_gstate.tyche_earn_contract, { {get_self(), "active"_n} });
      reward_refuel_act.send(token_bank, quant, DAY_SECONDS, pool_conf_code);
   }
}

//提出奖励
void tyche_reward::claimreward( const name& to, const name& bank, const asset& total_rewards, const string& memo){
   require_auth(_gstate.tyche_earn_contract);
   _sub_reward(total_rewards, bank);
   TRANSFER( bank, to, total_rewards, memo)
}

//提取利息
void tyche_reward::claimintr( const name& to, const name& bank, const asset& total_interest, const string& memo){
   require_auth(_gstate.tyche_earn_contract);
   _gstate.redeemed_interest_quant += total_interest;
   TRANSFER( bank, to, total_interest, memo)
}

void tyche_reward::onpoolstart(){
   require_auth(_self);
   _gstate.interest_splitted_at = current_time_point();
}

//触发利息,每天触发一次
void tyche_reward::splitintr(){
   auto elapsed =  (time_point_sec(current_time_point()) - _gstate.interest_splitted_at);
   auto elapsed_seconds = elapsed.count() / 1000000;
   CHECKC( elapsed_seconds > 0, err::TIME_PREMATURE, "time premature" )
   //获取本金总数
   auto pools         = earn_pool_t::tbl_t(_gstate.tyche_earn_contract, _gstate.tyche_earn_contract.value);
   auto pool_itr      = pools.begin();
   CHECKC( pool_itr != pools.end(), err::RECORD_NOT_FOUND, "earn pool not found" )
   auto total_quant  = asset(0, _gstate.total_interest_quant.symbol);
   while( pool_itr != pools.end()) {
      total_quant += pool_itr->avl_principal;
      pool_itr++;
   }
   auto total_interest = total_quant * _gstate.annual_interest_rate / YEAR_SECONDS * elapsed_seconds / PCT_BOOST;
   CHECKC(total_interest.amount > 10, err::INCORRECT_AMOUNT,  "interest amount too small: " + total_interest.to_string() )
   _gstate.allocated_interest_quant       += total_interest;
   _gstate.interest_splitted_at   = current_time_point();
   tyche_earn::onrefuelintrst_action interest_refuel_act(_gstate.tyche_earn_contract, { {get_self(), "active"_n} });
   interest_refuel_act.send(MUSDT_BANK, total_interest, elapsed_seconds);
}

//设置年化利率
void tyche_reward::setrate(uint64_t& rate){
   require_auth(_self);
   _gstate.annual_interest_rate = rate;
}

void tyche_reward::_save_reward_info(const asset& quant, const name& token_bank, const uint64_t& pool_conf_code){
   auto rewards = reward_t::tbl_t(_self, _self.value);
   auto reward_itr = rewards.find(quant.symbol.code().raw());
   if(reward_itr == rewards.end()) {
      rewards.emplace(_self, [&](auto& row) {
         row.total_reward_quant = quant;
         row.allocated_reward_quant = asset(0, quant.symbol);
         row.redeemed_reward_quant = asset(0, quant.symbol);
         row.bank = token_bank;
         row.memo = "reward: " + to_string(pool_conf_code);
         row.rewarded_at = current_time_point();
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

void tyche_reward::_sub_reward(const asset& quant, const name& token_bank){
   auto rewards = reward_t::tbl_t(_self, _self.value);
   auto reward_itr = rewards.find(quant.symbol.code().raw());
   CHECKC(reward_itr != rewards.end(), err::RECORD_NOT_FOUND, "reward not found")
   CHECKC(reward_itr->bank == token_bank, err::CONTRACT_MISMATCH, "bank mismatch")
   CHECKC(reward_itr->total_reward_quant >= quant, err::INCORRECT_AMOUNT, "reward not enough:"
    + reward_itr->total_reward_quant.to_string()
            + ", quant: " + quant.to_string())
   rewards.modify(reward_itr, _self, [&](auto& row) {
      row.total_reward_quant -= quant;
      row.updated_at = current_time_point();
   });
}  

// void tyche_reward::initrwd(const asset& quant){
//    auto rewards = reward_t::tbl_t(_self, _self.value);
//    auto reward_itr = rewards.find(quant.symbol.code().raw());
//    rewards.modify(reward_itr, _self, [&](auto& row) {
//       row.allocated_reward_quant = asset(0, quant.symbol);
//       row.redeemed_reward_quant = asset(0, quant.symbol);
//    });
// }

} //namespace tychefi