#include <usdt.save/usdt.save.hpp>
#include "safemath.hpp"
#include <utils.hpp>

#include <amax.token.hpp>

static constexpr eosio::name active_permission{"active"_n};

namespace amax {
using namespace std;
using namespace wasm::safemath;

#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + to_string((int)code) + string("]] ") + msg); }

   inline int64_t get_precision(const symbol &s) {
      int64_t digit = s.precision();
      CHECK(digit >= 0 && digit <= 18, "precision digit " + std::to_string(digit) + " should be in range[0,18]");
      return calc_precision(digit);
   }

inline int64_t get_precision(const asset &a) {
   return get_precision(a.symbol);
}

//根据新打入rewards来计算新的rewards_per_vote
inline static int128_t calc_rewards_per_vote(const int128_t& old_rewards_per_vote, const asset& rewards, const asset& total_votes) {
   ASSERT(rewards.amount >= 0 && total_votes.amount >= 0);
   int128_t new_rewards_per_vote = old_rewards_per_vote;
   if (rewards.amount > 0 && total_votes.amount > 0) {
      new_rewards_per_vote = old_rewards_per_vote + rewards.amount * HIGH_PRECISION / total_votes.amount;
      CHECK(new_rewards_per_vote >= old_rewards_per_vote, "calculated rewards_per_vote overflow")
   }
   return new_rewards_per_vote;
}
// 根据用户的votes和rewards_per_vote_delta来计算用户的rewards
inline static asset calc_voter_rewards(const asset& user_votes, const int128_t& rewards_per_vote_delta) {
   ASSERT( user_votes.amount >= 0 && rewards_per_vote_delta >= 0 );
   int128_t rewards = user_votes.amount * rewards_per_vote_delta / HIGH_PRECISION;
   CHECK( rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow" );
   return asset( (int64_t)rewards, MUSDT );
}

void usdt_save::init() {
   CHECK(false, "disabled" )
   require_auth( _self );
}

/**
 * @brief send nasset tokens into nftone marketplace
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo: two formats:
 *       1) refuel
 *       2) deposit:1d | 1m | 1q | 1y
 */
void usdt_save::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );

   // check(false, "under maintenance");
   
   if (from == get_self() || to != get_self()) return;
   auto token_bank = get_first_receiver();
    if(quant.symbol == _gstate.voucher_token.get_symbol()) { //提取奖励
      CHECKC( _gstate.voucher_token.get_contract() == token_bank, err::CONTRACT_MISMATCH, "interest token contract mismatches" )
      auto term_code = (uint64_t) stoi(memo);
      onredeem(from, term_code, quant );
      return;
   }

   vector<string_view> params = split(memo, ":");
   //用户充入本金
   if(params.size() == 2 && params[0] == "deposit" && quant.symbol == _gstate.principal_token.get_symbol()) {
      auto term_code = (uint64_t) stoi(string(params[1]));
      onuserdeposit(from, term_code, quant);
      return;
   } 
      
   //管理员打入利息
   if( memo == "refuel" ){
      CHECKC(from == _gstate.refuel_account, err::ACCOUNT_INVALID, "only refuel account can refuel")
      
      auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
      auto reward_symbol      = reward_symbols.find( quant.symbol.raw() );
      CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "save plan not found" )
      CHECKC( reward_symbol->on_self, err::RECORD_NOT_FOUND, "save plan not found" )
      onrewardrefuel( from, quant );
     return;
   }

}
//管理员打入奖励
void usdt_save::onrewardrefuel( const name& from, const asset& total_rewards ){
   auto confs           = save_conf_t::tbl_t(_self, _self.value);
   auto conf_itr        = confs.begin();
   CHECKC( conf_itr != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

   auto total_vote = 0;
   while( conf_itr != confs.end()) {
      if(conf_itr->on_self) 
         total_vote += conf_itr->votes_mutli * total_rewards.amount;
      conf_itr++;
   }
   CHECKC( total_vote > 0, err::INCORRECT_AMOUNT, "total vote is zero" )
   conf_itr        = confs.begin();
   while( conf_itr != confs.end() ){
      if( conf_itr->on_self ){
         auto rate = total_rewards.amount * conf_itr->votes_mutli * PCT_BOOST / total_vote;
         auto rewards = asset(total_rewards.amount * rate / PCT_BOOST, total_rewards.symbol);

         if(conf_itr->reward_confs.count(total_rewards.symbol.code().raw()) == 0) {
            confs.modify( conf_itr, _self, [&]( auto& c ) {
               auto reward_conf = reward_conf_t();
               reward_conf.total_rewards        = rewards;
               reward_conf.allocating_rewards   = rewards;
               reward_conf.allocated_rewards    = asset(0, total_rewards.symbol);
               reward_conf.claimed_rewards      = asset(0, total_rewards.symbol);
               reward_conf.rewards_per_vote = 0;
               // c.reward_conf[total_rewards.symbol.code().raw()] = reward_conf;
               //TODO
            });
         } else {
            confs.modify( conf_itr, _self, [&]( auto& c ) {
               auto older_reward = conf_itr->reward_confs.at(rewards.symbol.code().raw());
               older_reward.total_rewards        = older_reward.total_rewards + rewards;
               older_reward.allocating_rewards   = older_reward.allocating_rewards + rewards;
               older_reward.rewards_per_vote = calc_rewards_per_vote(older_reward.rewards_per_vote, rewards, conf_itr->total_deposit_quant);
               // c.reward_conf[total_rewards.symbol.code().raw()] = older_reward;
               // TODO
            });
         }
      }
      conf_itr++;
   }
}

   void usdt_save::onuserdeposit( const name& from, const uint64_t& team_code, const asset& quant ){
      CHECKC( _gstate.mini_deposit_amount <= quant, err::INCORRECT_AMOUNT, "deposit amount too small" )
      auto now = time_point_sec(current_time_point());
      CHECKC( _gstate.enabled, err::PAUSED, "not effective yet" )

      auto confs           = save_conf_t::tbl_t(_self, _self.value);
      auto conf            = confs.find( team_code );
      CHECKC( conf != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

      auto accts              = save_account_t::tbl_t(_self, team_code);
      auto acct               = accts.find( from.value );
      if( acct == accts.end() ) {
         confs.modify( conf, _self, [&]( auto& c ) {
            c.total_deposit_quant   += quant;
            c.remain_deposit_quant  += quant;
         });

         acct = accts.emplace( _self, [&]( auto& a ) {
            a.account                     = from;
            a.deposit_quant               = quant;
            a.total_deposit_quant         = quant;
            a.voted_rewards               = get_new_voted_reward_info( conf->reward_confs);
            a.created_at                  = now;
            a.started_at                  = now;
         });

      } else {
         //当用户充入本金, 要结算充入池子的用户之前的利息，同时要修改充入池子的基本信息
         //循环结算每一种利息代币

         auto older_depost_quant = acct->deposit_quant;
         for (auto& reward_conf_kv : conf->reward_confs) { //for循环每一个token
            auto reward_conf     = reward_conf_kv.second;
            auto code            = reward_conf_kv.first;
            auto voted_reward    = reward_info_t();
            auto new_rewards     = asset(0, reward_conf.total_rewards.symbol);
            if(acct->voted_rewards.count(reward_conf_kv.first)) {
               voted_reward = acct->voted_rewards.at(reward_conf_kv.first);
            } else {
               voted_reward.unclaimed_rewards      = asset(0, reward_conf.total_rewards.symbol);
               voted_reward.claimed_rewards        = asset(0, reward_conf.total_rewards.symbol);
               voted_reward.last_rewards_settled_at = now;
               voted_reward.last_rewards_per_vote  = 0;
            }
            int128_t rewards_per_vote_delta = reward_conf.rewards_per_vote - voted_reward.last_rewards_per_vote;
            if (rewards_per_vote_delta > 0 && older_depost_quant.amount > 0) {
               new_rewards = calc_voter_rewards(older_depost_quant, rewards_per_vote_delta);
               reward_conf.allocating_rewards         -= new_rewards;
               reward_conf.allocated_rewards          += new_rewards;
               voted_reward.last_rewards_settled_at   = now;
               voted_reward.last_rewards_per_vote     = reward_conf.rewards_per_vote;
               voted_reward.unclaimed_rewards         += quant;
               voted_reward.claimed_rewards           += quant;
            }
            //TODO
            // conf->reward_confs[code]                   = reward_conf;
            // acct->voted_rewards[code]                  = voted_reward;
            confs.modify( conf, _self, [&]( auto& c ) {
               c.total_deposit_quant   += quant;
               c.remain_deposit_quant  += quant;
               c.reward_confs = conf->reward_confs;
            });

            accts.modify( acct, _self,  [&]( auto& c ) {
               c.total_deposit_quant   += quant;
               c.deposit_quant         += quant;
               c.voted_rewards         = acct->voted_rewards;
               c.started_at            = now;
            });
         }
      }
      //transfer nusdt to user
      auto nusdt_quant =  asset(quant.amount, _gstate.voucher_token.get_symbol());
      //打出NUSDT
   }


   voted_reward_map usdt_save::get_new_voted_reward_info(const reward_conf_map& reward_confs) {
      voted_reward_map voted_rewards;
      for (auto& reward_conf : reward_confs) {
         reward_info_t reward_info;
         reward_info.last_rewards_per_vote = reward_conf.second.rewards_per_vote;
         reward_info.unclaimed_rewards = asset(0, reward_conf.second.total_rewards.symbol);
         reward_info.claimed_rewards = asset(0, reward_conf.second.total_rewards.symbol);
         reward_info.last_rewards_settled_at = time_point_sec(current_time_point());
         voted_rewards[reward_conf.first] = reward_info;
      
      }
      return voted_rewards;
   }

   //用户提款只能按全额来提款
   void usdt_save::onredeem( const name& from, const uint64_t& team_code, const asset& quant ){

      auto confs           = save_conf_t::tbl_t(_self, _self.value);
      auto conf            = confs.find( team_code );
      CHECKC( conf != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

      auto accts              = save_account_t::tbl_t(_self, team_code);
      auto acct               = accts.find( from.value );
      CHECKC( acct != accts.end(), err::RECORD_NOT_FOUND, "account not found" )

      auto now                = current_time_point();
      CHECKC(acct->deposit_quant.amount == quant.amount, err::INCORRECT_AMOUNT, "insufficient deposit amount" )

      // conf->reward_confs[quant.symbol.code().raw()];

      // CHECKC( conf->rewards_per_vote >= acct->last_rewards_per_vote, err::INCORRECT_AMOUNT, "rewards per vote error" )
      // int128_t rewards_per_vote_delta = conf->rewards_per_vote - acct->last_rewards_per_vote;
      // auto total_rewards = acct.unclaimed_rewards + calc_voter_rewards(acct.deposit_quant, rewards_per_vote_delta);

      //赎回每一种利息代币

      // confs.modify( conf, _self, [&]( auto& c ) {
      //    c.remain_deposit_quant.amount       -= quant.amount;
      //    c.last_deposit_at                   = now;
      //    if (rewards_per_vote_delta > 0 && votes_old.amount > 0) {
      //       auto new_rewards = calc_voter_rewards(votes_old, rewards_per_vote_delta);
      //       CHECK(conf.allocating_rewards >= new_rewards, "producer allocating rewards insufficient");
      //       c.allocating_rewards    -= total_rewards;
      //       c.allocated_rewards     += total_rewards;
      //    }
      // });

      // accts.modify( acct, _self, [&]( auto& a ) {
      //    a.deposit_quant.amount        -= quant.amount;
      //    a.unclaimed_rewards           = asset(0, MUSDT);
      //    a.claimed_rewards             += total_rewards;
      //    a.last_rewards_per_vote       = conf.rewards_per_vote;
      //    a.updated_at                  = now;
      // });

      // //transfer MUSDT to user
      // auto total_quant = asset(quant.amount + total_rewards.amount, redeem_interest.symbol);
   }

   void usdt_save::apl_reward(const asset& interest) {

   }
}