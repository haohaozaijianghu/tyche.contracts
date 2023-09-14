#include <usdt.save/usdt.save.hpp>
#include "safemath.hpp"
#include <utils.hpp>
#include <usdt_interest.hpp>
#include <custody.hpp>

#include <amax.token.hpp>
#include <aplink.farm/aplink.farm.hpp>

static constexpr eosio::name active_permission{"active"_n};

namespace amax {
using namespace std;
using namespace wasm::safemath;

#define ALLOT_APPLE(farm_contract, lease_id, to, quantity, memo) \
    {   aplink::farm::allot_action(farm_contract, { {_self, active_perm} }).send( \
            lease_id, to, quantity, memo );}

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

//根据新打入rewards来计算新的reward_per_share
inline static int128_t calc_reward_per_share_delta( const asset& rewards, const asset& total_shares) {
   ASSERT(rewards.amount >= 0 && total_shares.amount >= 0);
   int128_t new_reward_per_share_delta = 0;
   if (rewards.amount > 0 && total_shares.amount > 0) {
      new_reward_per_share_delta = rewards.amount * HIGH_PRECISION / total_shares.amount;
   }
   return new_reward_per_share_delta;
}
// 根据用户的votes和reward_per_share_delta来计算用户的rewards
inline static asset calc_sharer_rewards(const asset& user_shares, const int128_t& reward_per_share_delta, const symbol& rewards_symbol) {
   ASSERT( user_shares.amount >= 0 && reward_per_share_delta >= 0 );
   int128_t rewards = user_shares.amount * reward_per_share_delta / HIGH_PRECISION;
   rewards = rewards * get_precision(rewards_symbol)/get_precision(user_shares.symbol);
   CHECK( rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow" );
   return asset( (int64_t)rewards, rewards_symbol );
}

void usdt_save::init(const name& admin, const name& interest_contract, const name& trusd_refueler, const bool& enabled) {
   require_auth( _self );

   _gstate.admin                    = admin;
   _gstate.interest_contract   = interest_contract;
   _gstate.trusd_refueler           = trusd_refueler;
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
   if(quant.symbol == _gstate.lp_token.get_symbol()) { //提取奖励
      auto term_code = 0;
      CHECKC( _gstate.lp_token.get_contract() == token_bank, err::CONTRACT_MISMATCH, "interest token contract mismatches" )
      if(from == _gstate.trusd_refueler) //充入TRUSD到合约
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
   //TYCHE奖励充入
   if(quant.symbol == TYCHE && token_bank == TYCHE_BANK && from == _gstate.trusd_refueler) {
      return;
   }
   CHECKC(false, err::PARAM_ERROR, "invalid memo format")
}

//管理员打入奖励
void usdt_save::rewardrefuel( const name& token_bank, const asset& total_rewards ){
   require_auth(_gstate.interest_contract);
   auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
   auto reward_symbol      = reward_symbols.find( total_rewards.symbol.code().raw() );
   CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "reward symbol not found:" + total_rewards.to_string()  )
   CHECKC( token_bank == reward_symbol->sym.get_contract(), err::RECORD_NOT_FOUND, "bank not equal" )
   CHECKC( reward_symbol->on_shelf, err::RECORD_NOT_FOUND, "reward_symbol not on_shelf" )
   auto now             = time_point_sec(current_time_point());
   auto confs           = earn_pool_t::tbl_t(_self, _self.value);
   auto conf_itr        = confs.begin();
   CHECKC( conf_itr != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

   auto total_share = 0;
   while( conf_itr != confs.end()) {
      if(conf_itr->on_shelf) 
         total_share += conf_itr->share_multiplier * conf_itr->available_quant.amount;
      conf_itr++;
   }
   CHECKC( total_share > 0, err::INCORRECT_AMOUNT, "total vote is zero" )
   conf_itr        = confs.begin();

   while( conf_itr != confs.end() ){
      if( conf_itr->on_shelf ){
         auto rate = conf_itr->available_quant.amount * conf_itr->share_multiplier * PCT_BOOST / total_share;
         auto rewards = asset(total_rewards.amount * rate / PCT_BOOST, total_rewards.symbol);

         auto conf_id = _global_state->new_reward_conf_id();
         if(conf_itr->rewards.count(total_rewards.symbol.code().raw()) == 0) {
            confs.modify( conf_itr, _self, [&]( auto& c ) {
               auto reward_conf = earn_pool_reward_t();
               reward_conf.id                         = conf_id;
               reward_conf.total_rewards              = rewards;
               reward_conf.last_rewards               = rewards;
               reward_conf.unalloted_rewards          = rewards;
               reward_conf.unclaimed_rewards          = asset(0, total_rewards.symbol);
               reward_conf.claimed_rewards            = asset(0, total_rewards.symbol);
               reward_conf.prev_reward_added_at       = reward_conf.reward_added_at;
               reward_conf.reward_added_at            = now;
               
               c.rewards[total_rewards.symbol.code().raw()] = reward_conf;
            });
         } else {
            confs.modify( conf_itr, _self, [&]( auto& c ) {
               auto older_reward = conf_itr->rewards.at(rewards.symbol.code().raw());
               older_reward.id                           = conf_id;
               older_reward.total_rewards                = older_reward.total_rewards + rewards;
               older_reward.last_rewards                 = rewards;
               older_reward.unalloted_rewards            = older_reward.unalloted_rewards + rewards;
               older_reward.last_reward_per_share        = older_reward.reward_per_share ;
               older_reward.reward_per_share             = older_reward.reward_per_share + calc_reward_per_share_delta(rewards, conf_itr->sum_quant);
               older_reward.prev_reward_added_at         = older_reward.reward_added_at;
               older_reward.reward_added_at              = now;
               c.rewards[total_rewards.symbol.code().raw()] = older_reward;
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

      auto confs              = earn_pool_t::tbl_t(_self, _self.value);
      auto conf               = confs.find( team_code );
      CHECKC( conf != confs.end(), err::RECORD_NOT_FOUND, "save plan not found" )

      auto accts              = earner_t::tbl_t(_self, team_code);
      auto acct               = accts.find( from.value );
      if( acct == accts.end() ) {
         confs.modify( conf, _self, [&]( auto& c ) {
            c.sum_quant             += quant;
            c.available_quant       += quant;
         });

         acct = accts.emplace( _self, [&]( auto& a ) {
            a.owner                 = from;
            a.available_quant       = quant;
            a.sum_quant             = quant;
            a.earner_rewards        = get_new_shared_earner_reward( conf->rewards);
            a.created_at            = now;
            a.term_started_at       = now;
            a.term_end_at           = now + conf->term_interval_sec;
         });

      } else {
         //当用户充入本金, 要结算充入池子的用户之前的利息，同时要修改充入池子的基本信息
         //循环结算每一种利息代币
         earn_pool_reward_map rewards = conf->rewards;
         earner_reward_map earner_rewards  = acct->earner_rewards;
         auto older_deposit_quant = acct->available_quant;


         for (auto& reward_conf_kv : conf->rewards) { //for循环每一个token
            auto reward_conf     = reward_conf_kv.second;
            auto code            = reward_conf_kv.first;
            auto earner_reward    = earner_reward_t();
            auto new_rewards     = asset(0, reward_conf.total_rewards.symbol);
            //初始化earner_reward 信息
            if(acct->earner_rewards.count(reward_conf_kv.first)) {
               earner_reward = acct->earner_rewards.at(reward_conf_kv.first);
            } else {
               earner_reward.unclaimed_rewards         = asset(0, reward_conf.total_rewards.symbol);
               earner_reward.claimed_rewards           = asset(0, reward_conf.total_rewards.symbol);
               earner_reward.total_claimed_rewards     = asset(0, reward_conf.total_rewards.symbol);
               earner_reward.last_reward_per_share     = 0;
            }

            int128_t reward_per_share_delta = reward_conf.reward_per_share - earner_reward.last_reward_per_share;
            if (reward_per_share_delta > 0 && older_deposit_quant.amount > 0) {
               new_rewards = calc_sharer_rewards(older_deposit_quant, reward_per_share_delta, reward_conf.total_rewards.symbol);
               reward_conf.unalloted_rewards          -= new_rewards;
               reward_conf.unclaimed_rewards          += new_rewards;
               earner_reward.last_reward_per_share    = reward_conf.reward_per_share;
               earner_reward.unclaimed_rewards        += new_rewards;
            }
            rewards[code]                             = reward_conf;
            earner_rewards[code]                      = earner_reward;
         }
         confs.modify( conf, _self, [&]( auto& c ) {
               c.sum_quant          += quant;
               c.available_quant    += quant;
               c.rewards            = rewards;
         });

         accts.modify( acct, _self,  [&]( auto& c ) {
               c.sum_quant             += quant;
               c.available_quant       += quant;
               c.earner_rewards        = earner_rewards;
               c.term_started_at       = now;
               c.term_end_at           = now  + conf->term_interval_sec;
            });
      }
      //transfer nusdt to user
      TRANSFER( _gstate.lp_token.get_contract(), from, asset(quant.amount, _gstate.lp_token.get_symbol()), "depsit credential" )
      //TODO只有天池5号才有奖励
      if(team_code == _gstate.tyche_reward_term_code) {
         //打出TYCHE
         auto tyche_amount = quant.amount * _gstate.tyche_farm_ratio / PCT_BOOST;
         TRANSFER( TYCHE_BANK, from, asset(tyche_amount, TYCHE), "tyche farm reward" )
      }
   }

   earner_reward_map usdt_save::get_new_shared_earner_reward(const earn_pool_reward_map& rewards) {
      earner_reward_map earner_rewards;
      for (auto& reward_conf : rewards) {
         earner_reward_t earner_reward;
         earner_reward.last_reward_per_share = reward_conf.second.reward_per_share;
         earner_reward.unclaimed_rewards = asset(0, reward_conf.second.total_rewards.symbol);
         earner_reward.claimed_rewards = asset(0, reward_conf.second.total_rewards.symbol);
         earner_reward.total_claimed_rewards = asset(0, reward_conf.second.total_rewards.symbol);
         earner_rewards[reward_conf.first] = earner_reward;
      }
      return earner_rewards;
   }

   //用户提款只能按全额来提款
   void usdt_save::onredeem( const name& from, const uint64_t& team_code, const asset& quant ){
      auto confs  = earn_pool_t::tbl_t(_self, _self.value);
      auto conf   = confs.find( team_code );
      CHECKC( conf != confs.end(), err::RECORD_NOT_FOUND, "save conf not found" )

      auto accts  = earner_t::tbl_t(_self, team_code);
      auto acct   = accts.find( from.value );
      CHECKC( acct != accts.end(), err::RECORD_NOT_FOUND, "account not found" )

      auto now    = current_time_point();
      CHECKC(acct->available_quant.amount == quant.amount, err::INCORRECT_AMOUNT, "insufficient deposit amount" )
      
      auto rewards = conf->rewards;
      auto vote_rewards = acct->earner_rewards;
      for (auto& reward_conf_kv : conf->rewards) { //for循环每一个token
         auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
         auto reward_conf        = reward_conf_kv.second;
         auto code               = reward_conf_kv.first;
         auto reward_symbol      = reward_symbols.find( reward_conf.total_rewards.symbol.code().raw());
         CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "save plan not found: " + reward_conf.total_rewards.symbol.code().to_string() )
         CHECKC( reward_symbol->on_shelf, err::RECORD_NOT_FOUND, "save plan not on self: "+ reward_conf.total_rewards.symbol.code().to_string() )
         earner_reward_t earner_reward = {0, asset(0, reward_conf.total_rewards.symbol), asset(0, reward_conf.total_rewards.symbol)};
         if(acct->earner_rewards.count(code)) {
            earner_reward  = acct->earner_rewards.at(code);
         }
         int128_t reward_per_share_delta = reward_conf.reward_per_share - earner_reward.last_reward_per_share;

         auto new_rewards        = calc_sharer_rewards(acct->available_quant, reward_per_share_delta, reward_conf.total_rewards.symbol);
         auto total_rewards      = new_rewards + earner_reward.unclaimed_rewards;

         reward_conf.unalloted_rewards   -= new_rewards;
         reward_conf.unclaimed_rewards    += new_rewards;
         rewards[code]                    = reward_conf;

         earner_reward.unclaimed_rewards           =  asset(0, total_rewards.symbol);
         earner_reward.claimed_rewards             += asset(0, total_rewards.symbol);
         earner_reward.total_claimed_rewards       += total_rewards;
         earner_reward.last_reward_per_share       =  reward_conf.reward_per_share;
         vote_rewards[code]                        =  earner_reward;
         //内部调用发放利息
         //发放利息
         usdt_interest::claimreward_action cliam_reward_act(_gstate.interest_contract, { {get_self(), "active"_n} });
         cliam_reward_act.send(from, reward_symbol->sym.get_contract(), total_rewards, "interest");
      }

      confs.modify( conf, _self, [&]( auto& c ) {
         c.available_quant.amount -= quant.amount;
         c.rewards                = rewards;
      });
      accts.modify( acct, _self, [&]( auto& a ) {
         a.available_quant.amount         -= quant.amount;
         a.earner_rewards                 = vote_rewards;
         a.term_started_at                = now;
         a.term_end_at                    = now + seconds(conf->term_interval_sec);
      });
      CHECKC(acct->term_started_at + conf->term_interval_sec > now, err::TIME_PREMATURE, "not due")
      //打出本金MUSDT
      TRANSFER( MUSDT_BANK, from, asset(quant.amount, MUSDT), "redeem" )

       if(team_code == _gstate.tyche_reward_term_code) {
         //打出TYCHE
         auto tyche_amount = quant.amount * _gstate.tyche_farm_lock_ratio / PCT_BOOST;
         TRANSFER( TYCHE_BANK, from, asset(tyche_amount, TYCHE), "tyche farm reward" )
      }
   }

   void usdt_save::addrewardsym(const extended_symbol& sym, const uint64_t& interval, const name& reward_type) {
      require_auth(_self);
      auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
      auto reward_symbol      = reward_symbols.find( sym.get_symbol().code().raw() );
      CHECKC( reward_symbol == reward_symbols.end(), err::RECORD_EXISTING, "save plan not found" )
      CHECKC( reward_type == INTEREST || reward_type == REDPACK, err::PARAM_ERROR, "reward_type error" )
      reward_symbols.emplace( _self, [&]( auto& s ) {
         s.sym                         = sym;
         s.claim_term_interval_sec     = interval;
         s.reward_type                 = reward_type;
         s.on_shelf                    = false;
      });
   }

     void usdt_save::symonself(const extended_symbol& sym, const bool& on_shelf) {
      require_auth(_self);
      auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
      auto reward_symbol      = reward_symbols.find( sym.get_symbol().code().raw() );
      CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "reward symbol not found" )
      reward_symbols.modify( reward_symbol, _self, [&]( auto& s ) {
         s.on_shelf               = on_shelf;
      });
   }

   void usdt_save::addsaveconf(const uint64_t& code, const uint64_t& term_interval_sec, const uint64_t& share_multiplier) {
      require_auth(_self);
      auto confs           = earn_pool_t::tbl_t(_self, _self.value);
      auto conf            = confs.find( code );
      if( conf == confs.end() ) {
         confs.emplace( _self, [&]( auto& c ) {
            c.code           = code;
            c.term_interval_sec  = term_interval_sec;
            c.share_multiplier    = share_multiplier;
            c.on_shelf        = true;
         });
      } else {
         confs.modify( conf, _self, [&]( auto& c ) {
            c.term_interval_sec  = term_interval_sec;
            c.share_multiplier    = share_multiplier;
            c.on_shelf        = true;
         });
      }
   }

   void  usdt_save::claimreward(const name& from, const uint64_t& team_code, const symbol& sym ){
      require_auth(from);
      auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
      auto code               =  sym.code().raw();
      auto reward_symbol      = reward_symbols.find( code );
      CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "save plan not found" )
      CHECKC( reward_symbol->on_shelf, err::RECORD_NOT_FOUND, "save plan not on self" )
      CHECKC( reward_symbol->reward_type == REDPACK, err::RECORD_NOT_FOUND, "save plan not on self" )
      // auto interval =  reward_symbol->term_interval_sec;
      auto now = time_point_sec(current_time_point());
      auto confs  = earn_pool_t::tbl_t(_self, _self.value);
      auto conf   = confs.find( team_code );
      CHECKC( conf != confs.end(), err::RECORD_NOT_FOUND, "save conf not found" )

      auto accts  = earner_t::tbl_t(_self, team_code);
      auto acct   = accts.find( from.value );
      CHECKC( acct != accts.end(), err::RECORD_NOT_FOUND, "account not found" )

      CHECKC(conf->rewards.count( code ),  err::RECORD_NOT_FOUND, "reward conf not found" )
      CHECKC(acct->earner_rewards.count( code ),  err::RECORD_NOT_FOUND, "reward not found" )
      
      auto earner_reward    = acct->earner_rewards.at(code);
      auto reward_conf     = conf->rewards.at(code);
      auto earner_rewards    = acct->earner_rewards;
      auto rewards    = conf->rewards;
      int128_t reward_per_share_delta = reward_conf.reward_per_share - earner_reward.last_reward_per_share;
      auto new_rewards     = calc_sharer_rewards(acct->available_quant, reward_per_share_delta, reward_conf.total_rewards.symbol);
      auto total_rewards   = new_rewards + earner_reward.unclaimed_rewards;
      reward_conf.unalloted_rewards   -= new_rewards;
      reward_conf.unclaimed_rewards    += new_rewards;
      rewards[code]               = reward_conf;

      earner_reward.unclaimed_rewards        =  asset(0, total_rewards.symbol);
      earner_reward.claimed_rewards          =  asset(0, total_rewards.symbol);
      earner_reward.total_claimed_rewards    += total_rewards;
      earner_reward.last_reward_per_share     =  reward_conf.reward_per_share;
      earner_rewards[code]                    =  earner_reward;
      
      confs.modify( conf, _self, [&]( auto& c ) {
         c.rewards                = rewards;
      });
      accts.modify( acct, _self, [&]( auto& a ) {
         a.earner_rewards                 = earner_rewards;
         a.term_started_at                = now;
         a.term_end_at                    = now + conf->term_interval_sec;
      });

      //内部调用发放利息
      //发放利息
      usdt_interest::claimreward_action cliam_reward_act(_gstate.interest_contract, { {get_self(), "active"_n} });
      cliam_reward_act.send(from, reward_symbol->sym.get_contract(), total_rewards, "interest");
   }
   
   void usdt_save::apl_reward(const name& from, const asset& interest) {
      auto apl_amount = interest.amount/get_precision(interest) * get_precision(APLINK_SYMBOL);
      asset apls = asset(apl_amount, APLINK_SYMBOL);
      ALLOT_APPLE( _gstate.apl_farm.contract, _gstate.apl_farm.lease_id, from, apls, "truedex creator reward" )
   }

   void usdt_save::setaplconf( const uint64_t& lease_id, const asset& unit_reward ){
      require_auth(_self);
      _gstate.apl_farm.lease_id     = lease_id;
      _gstate.apl_farm.unit_reward  = unit_reward;
   }


}