#include <usdt.save/usdt.save.hpp>
#include "safemath.hpp"
#include <utils.hpp>
#include <usdt_interest.hpp>
#include <custody.hpp>

#include <amax.token.hpp>

static constexpr eosio::name active_permission{"active"_n};

namespace amax {
using namespace std;
using namespace wasm::safemath;


#define ADD_ISSUE(contract, receiver, ido_id, quantity) \
    {	custody::add_issue_action act{ contract, { {_self, active_permission} } };\
            act.send( receiver, ido_id, quantity );}

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
inline static int128_t calc_rewards_per_vote_delta(const int128_t& old_rewards_per_vote, const asset& rewards, const asset& total_votes) {
   ASSERT(rewards.amount >= 0 && total_votes.amount >= 0);
   int128_t new_rewards_per_vote_delta = 0;
   if (rewards.amount > 0 && total_votes.amount > 0) {
      new_rewards_per_vote_delta = old_rewards_per_vote + rewards.amount * HIGH_PRECISION / total_votes.amount;
   }
   return new_rewards_per_vote_delta;
}
// 根据用户的votes和rewards_per_vote_delta来计算用户的rewards
inline static asset calc_voter_rewards(const asset& user_votes, const int128_t& rewards_per_vote_delta, const symbol& rewards_symbol) {
   ASSERT( user_votes.amount >= 0 && rewards_per_vote_delta >= 0 );
   int128_t rewards = user_votes.amount * rewards_per_vote_delta / HIGH_PRECISION;
   rewards = rewards * get_precision(rewards_symbol)/get_precision(user_votes.symbol);
   CHECK( rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow" );
   return asset( (int64_t)rewards, rewards_symbol );
}

void usdt_save::init(const name& admin, const name& usdt_interest_contract, const name& nusdt_refueler, const uint64_t& apl_multi, const bool& enabled) {
   require_auth( _self );

   _gstate.admin                    = admin;
   _gstate.usdt_interest_contract   = usdt_interest_contract;
   _gstate.nusdt_refueler           = nusdt_refueler;
   _gstate.apl_multi                = apl_multi;
   _gstate.enabled                  = enabled;
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

   if (from == get_self() || to != get_self()) return;
   auto token_bank = get_first_receiver();
   if(quant.symbol == _gstate.voucher_token.get_symbol()) { //提取奖励
      auto term_code = 0;
      CHECKC( _gstate.voucher_token.get_contract() == token_bank, err::CONTRACT_MISMATCH, "interest token contract mismatches" )
      if(from == _gstate.nusdt_refueler) //充入NUSDT到合约
         return;
      //用户提取奖励和本金
      term_code = (uint64_t) stoi(memo);
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
   CHECKC(false, err::PARAM_ERROR, "invalid memo format")
}

//管理员打入奖励
void usdt_save::rewardrefuel( const name& token_bank, const asset& total_rewards ){
   require_auth(_gstate.usdt_interest_contract);
   auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
   auto reward_symbol      = reward_symbols.find( total_rewards.symbol.code().raw() );
   CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "reward symbol not found:" + total_rewards.to_string()  )
   CHECKC( token_bank == reward_symbol->sym.get_contract(), err::RECORD_NOT_FOUND, "bank not equal" )
   CHECKC( reward_symbol->on_self, err::RECORD_NOT_FOUND, "reward_symbol not on_self" )
   auto now             = time_point_sec(current_time_point());
   auto confs           = save_conf_t::tbl_t(_self, _self.value);
   auto conf_itr        = confs.begin();
   CHECKC( conf_itr != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

   auto total_vote = 0;
   while( conf_itr != confs.end()) {
      if(conf_itr->on_self) 
         total_vote += conf_itr->votes_mutli * conf_itr->remain_deposit_quant.amount;
      conf_itr++;
   }
   CHECKC( total_vote > 0, err::INCORRECT_AMOUNT, "total vote is zero" )
   conf_itr        = confs.begin();

   while( conf_itr != confs.end() ){
      if( conf_itr->on_self ){
         auto rate = conf_itr->remain_deposit_quant.amount * conf_itr->votes_mutli * PCT_BOOST / total_vote;
         auto rewards = asset(total_rewards.amount * rate / PCT_BOOST, total_rewards.symbol);
      
         if(conf_itr->reward_confs.count(total_rewards.symbol.code().raw()) == 0) {
            confs.modify( conf_itr, _self, [&]( auto& c ) {
               auto reward_conf = reward_conf_t();
               reward_conf.total_rewards              = rewards;
               reward_conf.allocating_rewards         = rewards;
               reward_conf.allocated_rewards          = asset(0, total_rewards.symbol);
               reward_conf.claimed_rewards            = asset(0, total_rewards.symbol);
               reward_conf.last_reward_interval       = 0;
               reward_conf.last_reward_per_vote_delta = 0;
               reward_conf.last_rewards_settled_at    = now;
               
               c.reward_confs[total_rewards.symbol.code().raw()] = reward_conf;
            });
         } else {
            confs.modify( conf_itr, _self, [&]( auto& c ) {
               auto older_reward = conf_itr->reward_confs.at(rewards.symbol.code().raw());
               older_reward.total_rewards                = older_reward.total_rewards + rewards;
               older_reward.allocating_rewards           = older_reward.allocating_rewards + rewards;
               older_reward.last_reward_per_vote_delta   = calc_rewards_per_vote_delta(older_reward.rewards_per_vote, rewards, conf_itr->total_deposit_quant);
               older_reward.rewards_per_vote             = older_reward.rewards_per_vote + older_reward.last_reward_per_vote_delta;
               older_reward.last_rewards_settled_at      = now;
               older_reward.last_reward_interval         = now.sec_since_epoch() - older_reward.last_rewards_settled_at.sec_since_epoch();
               c.reward_confs[total_rewards.symbol.code().raw()] = older_reward;
            });
         }
      }
      conf_itr++;
   }
}

   void usdt_save::onuserdeposit( const name& from, const uint64_t& team_code, const asset& quant ){
      CHECKC( quant.symbol == _gstate.principal_token.get_symbol(), err::SYMBOL_MISMATCH, "symbol mismatch" )
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

         reward_conf_map reward_confs = conf->reward_confs;
         voted_reward_map voted_rewards  = acct->voted_rewards;
         auto older_depost_quant = acct->deposit_quant;
         for (auto& reward_conf_kv : conf->reward_confs) { //for循环每一个token
            auto reward_conf     = reward_conf_kv.second;
            auto code            = reward_conf_kv.first;
            auto voted_reward    = reward_info_t();
            auto new_rewards     = asset(0, reward_conf.total_rewards.symbol);
            //初始化voted_reward 信息
            if(acct->voted_rewards.count(reward_conf_kv.first)) {
               voted_reward = acct->voted_rewards.at(reward_conf_kv.first);
            } else {
               voted_reward.unclaimed_rewards         = asset(0, reward_conf.total_rewards.symbol);
               voted_reward.claimed_rewards           = asset(0, reward_conf.total_rewards.symbol);
               voted_reward.last_rewards_settled_at   = now;
               voted_reward.last_rewards_per_vote     = 0;
            }

            int128_t rewards_per_vote_delta = reward_conf.rewards_per_vote - voted_reward.last_rewards_per_vote;
            if (rewards_per_vote_delta > 0 && older_depost_quant.amount > 0) {
               new_rewards = calc_voter_rewards(older_depost_quant, rewards_per_vote_delta, reward_conf.total_rewards.symbol);
               reward_conf.allocating_rewards         -= new_rewards;
               reward_conf.allocated_rewards          += new_rewards;
               voted_reward.last_rewards_settled_at   = now;
               voted_reward.last_rewards_per_vote     = reward_conf.rewards_per_vote;
               voted_reward.unclaimed_rewards         += quant;
               voted_reward.claimed_rewards           += quant;
            }
            reward_confs[code]                   = reward_conf;
            voted_rewards[code]                  = voted_reward;
         }
         confs.modify( conf, _self, [&]( auto& c ) {
               c.total_deposit_quant   += quant;
               c.remain_deposit_quant  += quant;
               c.reward_confs = reward_confs;
         });

         accts.modify( acct, _self,  [&]( auto& c ) {
               c.total_deposit_quant   += quant;
               c.deposit_quant         += quant;
               c.voted_rewards         = voted_rewards;
               c.started_at            = now;
            });
      }
      //transfer nusdt to user
      TRANSFER( _gstate.voucher_token.get_contract(), from, asset(quant.amount, _gstate.voucher_token.get_symbol()), "depsit credential" )
      //打出TWGT
      auto tgt_amount = quant.amount * _gstate.reward_twgt_ratio / PCT_BOOST;
      TRANSFER( TWGT_BANK, from, asset(tgt_amount, TWGT), "depsit credential" )
      //进入到锁仓合约
      auto tgt_amount_lock_amount = quant.amount * _gstate.locked_reward_twgt_ratio / PCT_BOOST;
      ADD_ISSUE(_gstate.custody_contract, from,  _gstate.custody_id, asset(tgt_amount_lock_amount, TWGT))
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
      auto confs  = save_conf_t::tbl_t(_self, _self.value);
      auto conf   = confs.find( team_code );
      CHECKC( conf != confs.end(), err::RECORD_NOT_FOUND, "save conf not found" )

      auto accts  = save_account_t::tbl_t(_self, team_code);
      auto acct   = accts.find( from.value );
      CHECKC( acct != accts.end(), err::RECORD_NOT_FOUND, "account not found" )

      auto now    = current_time_point();
      CHECKC(acct->deposit_quant.amount == quant.amount, err::INCORRECT_AMOUNT, "insufficient deposit amount" )
      
      auto reward_confs = conf->reward_confs;
      auto vote_rewards = acct->voted_rewards;
      for (auto& reward_conf_kv : conf->reward_confs) { //for循环每一个token
         auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
         auto reward_conf        = reward_conf_kv.second;
         auto code               = reward_conf_kv.first;
         auto reward_symbol      = reward_symbols.find( reward_conf.total_rewards.symbol.code().raw());
         CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "save plan not found: " + reward_conf.total_rewards.symbol.code().to_string() )
         CHECKC( reward_symbol->on_self, err::RECORD_NOT_FOUND, "save plan not on self: "+ reward_conf.total_rewards.symbol.code().to_string() )
         reward_info_t voted_reward = {0, asset(0, reward_conf.total_rewards.symbol), asset(0, reward_conf.total_rewards.symbol), now};
         if(acct->voted_rewards.count(code)) {
            voted_reward  = acct->voted_rewards.at(code);
         }
         int128_t rewards_per_vote_delta = reward_conf.rewards_per_vote - voted_reward.last_rewards_per_vote;

         auto new_rewards        = calc_voter_rewards(acct->deposit_quant, rewards_per_vote_delta, reward_conf.total_rewards.symbol);
         auto total_rewards      = new_rewards + voted_reward.unclaimed_rewards;

         reward_conf.allocating_rewards   -= new_rewards;
         reward_conf.allocated_rewards    += new_rewards;
         reward_confs[code]               = reward_conf;

         voted_reward.unclaimed_rewards         =  asset(0, total_rewards.symbol);
         voted_reward.claimed_rewards           += total_rewards;
         voted_reward.last_rewards_per_vote    =  reward_conf.rewards_per_vote;
         vote_rewards[code]                     =  voted_reward;
         //内部调用发放利息
         //发放利息
         usdt_interest::claimreward_action cliam_reward_act(_gstate.usdt_interest_contract, { {get_self(), "active"_n} });
         cliam_reward_act.send(from, reward_symbol->sym.get_contract(), total_rewards);
      }

      confs.modify( conf, _self, [&]( auto& c ) {
         c.remain_deposit_quant.amount -= quant.amount;
         c.reward_confs                = reward_confs;
      });
      accts.modify( acct, _self, [&]( auto& a ) {
         a.deposit_quant.amount        -= quant.amount;
         a.voted_rewards               = vote_rewards;
         a.started_at                  = now;
      });
      //打出本金MUSDT
      TRANSFER( MUSDT_BANK, from, asset(quant.amount, MUSDT), "redeem" )
   }

   void usdt_save::addrewardsym(const extended_symbol& sym, const uint64_t& interval) {
      require_auth(_self);
      auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
      auto reward_symbol      = reward_symbols.find( sym.get_symbol().code().raw() );
      CHECKC( reward_symbol == reward_symbols.end(), err::RECORD_EXISTING, "save plan not found" )
      reward_symbols.emplace( _self, [&]( auto& s ) {
         s.sym                   = sym;
         s.claim_term_interval   = interval;
         s.total_reward_quant    = asset(0, sym.get_symbol());
         s.reward_type           = name("interest");
         s.on_self               = false;
      });
   }

     void usdt_save::symonself(const extended_symbol& sym, const bool& on_self) {
      require_auth(_self);
      auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
      auto reward_symbol      = reward_symbols.find( sym.get_symbol().code().raw() );
      CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "reward symbol not found" )
      reward_symbols.modify( reward_symbol, _self, [&]( auto& s ) {
         s.on_self               = on_self;
      });
   }

   void usdt_save::addsaveconf(const uint64_t& code, const uint64_t& term_interval, const uint64_t& votes_mutli) {
      require_auth(_self);
      auto confs           = save_conf_t::tbl_t(_self, _self.value);
      auto conf            = confs.find( code );
      if( conf == confs.end() ) {
         confs.emplace( _self, [&]( auto& c ) {
            c.code           = code;
            c.term_interval  = term_interval;
            c.votes_mutli    = votes_mutli;
            c.on_self        = true;
         });
      } else {
         confs.modify( conf, _self, [&]( auto& c ) {
            c.term_interval  = term_interval;
            c.votes_mutli    = votes_mutli;
            c.on_self        = true;
         });
      }
   }

   void  usdt_save::claimreward(const name& from, const uint64_t& team_code, const symbol& sym ){
      require_auth(from);
      auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
      auto code               =  sym.code().raw();
      auto reward_symbol      = reward_symbols.find( code );
      CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "save plan not found" )
      CHECKC( reward_symbol->on_self, err::RECORD_NOT_FOUND, "save plan not on self" )
      // auto interval =  reward_symbol->term_interval;
      auto now = time_point_sec(current_time_point());
      auto confs  = save_conf_t::tbl_t(_self, _self.value);
      auto conf   = confs.find( team_code );
      CHECKC( conf != confs.end(), err::RECORD_NOT_FOUND, "save conf not found" )

      auto accts  = save_account_t::tbl_t(_self, team_code);
      auto acct   = accts.find( from.value );
      CHECKC( acct != accts.end(), err::RECORD_NOT_FOUND, "account not found" )

      CHECKC(conf->reward_confs.count( code ),  err::RECORD_NOT_FOUND, "reward conf not found" )
      CHECKC(acct->voted_rewards.count( code ),  err::RECORD_NOT_FOUND, "reward not found" )
      
      auto voted_reward    = acct->voted_rewards.at(code);
      auto reward_conf     = conf->reward_confs.at(code);
      auto voted_rewards    = acct->voted_rewards;
      auto reward_confs    = conf->reward_confs;
      int128_t rewards_per_vote_delta = reward_conf.rewards_per_vote - voted_reward.last_rewards_per_vote;
      auto new_rewards     = calc_voter_rewards(acct->deposit_quant, rewards_per_vote_delta, reward_conf.total_rewards.symbol);
      auto total_rewards   = new_rewards + voted_reward.unclaimed_rewards;
      reward_conf.allocating_rewards   -= new_rewards;
      reward_conf.allocated_rewards    += new_rewards;
      reward_confs[code]               = reward_conf;

      voted_reward.unclaimed_rewards         =  asset(0, total_rewards.symbol);
      voted_reward.claimed_rewards           += total_rewards;
      voted_reward.last_rewards_per_vote     =  reward_conf.rewards_per_vote;
      voted_rewards[code]                    =  voted_reward;
      
      confs.modify( conf, _self, [&]( auto& c ) {
         c.reward_confs                = reward_confs;
      });
      accts.modify( acct, _self, [&]( auto& a ) {
         a.voted_rewards               = voted_rewards;
         a.started_at                  = now;
      });

      //内部调用发放利息
      //发放利息
      usdt_interest::claimreward_action cliam_reward_act(_gstate.usdt_interest_contract, { {get_self(), "active"_n} });
      cliam_reward_act.send(from, reward_symbol->sym.get_contract(), total_rewards);
   }
   
   void usdt_save::apl_reward(const asset& interest) {

   }

}