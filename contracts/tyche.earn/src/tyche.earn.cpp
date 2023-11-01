#include <tyche.earn/tyche.earn.hpp>
#include <tyche.reward/tyche.reward.hpp>
#include <amax.token.hpp>
#include <aplink.farm/aplink.farm.hpp>

#include "safemath.hpp"
#include <utils.hpp>
// #include <eosiolib/time.hpp> 
#include <eosio/time.hpp>
#include<tyche.reward/tyche.reward.db.hpp>

static constexpr eosio::name active_permission{"active"_n};

namespace tychefi {
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
inline static asset calc_sharer_rewards(const asset& earner_shares, const int128_t& reward_per_share_delta, const symbol& rewards_symbol) {
   ASSERT( earner_shares.amount >= 0 && reward_per_share_delta >= 0 );
   int128_t rewards = earner_shares.amount * reward_per_share_delta / HIGH_PRECISION;
   // rewards = rewards * get_precision(rewards_symbol)/get_precision(earner_shares.symbol);
   CHECK( rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow" );
   return asset( (int64_t)rewards, rewards_symbol );
}

void tyche_earn::init(const name& admin, const name& reward_contract, const name& lp_refueler, const bool& enabled) {
   require_auth( _self );
   _gstate.admin                    = admin;
   _gstate.reward_contract          = reward_contract;
   _gstate.lp_refueler              = lp_refueler;
   _gstate.enabled                  = enabled;
}

/**
 * @brief send nasset tokens into nftone marketplace
 *
 * @param from
 * @param to
 * @param quantity
 * @param memo: two formats:
 *       1) redeem:$code - upon transferring in TRUSD to withdraw
 *       2) deposit:$code  - codes: 1,30,90,180,360
 */
void tyche_earn::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   CHECKC(_gstate.enabled, err::PAUSED, "not effective yet");
   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );

   if (from == get_self() || to != get_self()) return;
   auto token_bank = get_first_receiver();

   if( quant.symbol == _gstate.lp_token.get_symbol() ) { //提取奖励
      CHECKC( _gstate.lp_token.get_contract() == token_bank, err::CONTRACT_MISMATCH, "LP token contract mismatches" )
      if(from == _gstate.lp_refueler) //充入TRUSD到合约
         return;

      vector<string_view> params = split(memo, ":");
      CHECKC( params.size() == 2 && params[0] == "redeem", err::MEMO_FORMAT_ERROR, "redeem memo format error" )

      //用户提取奖励和本金
      auto term_code = (uint64_t) stoi(string(params[1]));
      onredeem(from, term_code, quant );
      return;
   }

   vector<string_view> params = split(memo, ":");
   //用户充入本金
   if(params.size() == 2 && params[0] == "deposit" && quant.symbol == _gstate.principal_token.get_symbol()) {
      auto term_code = (uint64_t) stoi(string(params[1]));
      ondeposit(from, term_code, quant);
      return;
   }
   //TYCHE奖励充入
   if(quant.symbol == TYCHE && token_bank == TYCHE_BANK && from == _gstate.lp_refueler) {
      return;
   }
   CHECKC(false, err::PARAM_ERROR, "invalid memo format")
}

//管理员打入奖励
void tyche_earn::refuelreward( const name& token_bank, const asset& total_rewards, const uint64_t& seconds, const uint64_t& pool_conf_code){
   require_auth(_gstate.reward_contract);
   if(pool_conf_code == 0)
      refuelreward_to_all(token_bank, total_rewards, seconds);
   else
      refuelreward_to_pool(token_bank, total_rewards, seconds, pool_conf_code);
}

void tyche_earn::refuelintrst( const name& token_bank, const asset& total_rewards, const uint64_t& seconds){ 
   require_auth(_gstate.reward_contract);
   CHECKC( token_bank == MUSDT_BANK, err::RECORD_NOT_FOUND, "bank not equal" )
   auto now             = time_point_sec(current_time_point());
   auto pools           = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr        = pools.begin();
   CHECKC( pool_itr != pools.end(), err::RECORD_NOT_FOUND, "save plan not found" )

   uint64_t total_share = 0;
   while( pool_itr != pools.end()) {
      if(pool_itr->on_shelf) 
         total_share += pool_itr->share_multiplier * pool_itr->avl_principal.amount;
      pool_itr++;
   }
   CHECKC( total_share > 0, err::INCORRECT_AMOUNT, "total vote is zero" )
   pool_itr        = pools.begin();
   while( pool_itr != pools.end() ){
      if( !pool_itr->on_shelf ) {pool_itr++; continue;}

      auto rate = pool_itr->avl_principal.amount * pool_itr->share_multiplier * PCT_BOOST / total_share;
      CHECKC(rate <= PCT_BOOST, err::OVERSIZED, "rate must < PCT_BOOST" )
      auto rewards = asset(total_rewards.amount * rate / PCT_BOOST, total_rewards.symbol);
      auto new_reward_id = _global_state->new_reward_id();
      if( rewards.amount > 0) {
         auto last_reward = pool_itr->interest_reward;
         
         pools.modify( pool_itr, _self, [&]( auto& c ) {
            last_reward.reward_id                     = new_reward_id;
            last_reward.total_rewards                 += rewards;
            last_reward.last_rewards                  = rewards;
            last_reward.unalloted_rewards             += rewards;
            last_reward.last_reward_per_share         = last_reward.reward_per_share;
            last_reward.reward_per_share              = last_reward.reward_per_share + calc_reward_per_share_delta(rewards, pool_itr->avl_principal);
            last_reward.prev_reward_added_at          = last_reward.reward_added_at;
            last_reward.reward_added_at               = now;
            c.interest_reward                         = last_reward;
         });
      }
      pool_itr++;
   }
   
}

void tyche_earn::refuelreward_to_pool( const name& token_bank, const asset& total_rewards, const uint64_t& seconds,const uint64_t& pool_conf_code ){
   auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
   auto reward_symbol      = reward_symbols.find( total_rewards.symbol.code().raw() );
   CHECKC( total_rewards.amount > 0,                        err::INCORRECT_AMOUNT, "total_rewards must be positive: "+ total_rewards.to_string() )
   CHECKC( reward_symbol != reward_symbols.end(),           err::RECORD_NOT_FOUND, "reward symbol not found:" + total_rewards.to_string()  )
   CHECKC( token_bank == reward_symbol->sym.get_contract(), err::RECORD_NOT_FOUND, "bank not equal" )
   CHECKC( reward_symbol->on_shelf,                         err::RECORD_NOT_FOUND, "reward_symbol not on_shelf" )
   auto pools              = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr           = pools.find( pool_conf_code );
   CHECKC( pool_itr != pools.end(), err::RECORD_NOT_FOUND, "save plan not found" )
   CHECKC( pool_itr->on_shelf, err::RECORD_NOT_FOUND, "save plan not on_shelf" )

   pools.modify( pool_itr, _self, [&]( auto& c ) {
   
         
         auto count =  c.airdrop_rewards.count(total_rewards.symbol);
         if(count == 0 ){
            auto reward =   earn_pool_reward_st();
            reward.reward_id                       = _global_state->new_reward_id();
            reward.total_rewards                   = total_rewards;
            reward.last_rewards                    = total_rewards;
            reward.unalloted_rewards               = total_rewards;
            reward.unclaimed_rewards               = asset(0, total_rewards.symbol);
            reward.claimed_rewards                 = asset(0, total_rewards.symbol);
            reward.last_reward_per_share           = 0; 
            reward.reward_per_share                = calc_reward_per_share_delta(total_rewards, pool_itr->avl_principal);
            reward.prev_reward_added_at            = current_time_point();
            reward.reward_added_at                 = current_time_point();
            c.airdrop_rewards[ total_rewards.symbol ] = reward;
         } else {
            auto  reward                           =  c.airdrop_rewards[ total_rewards.symbol ];
            reward.reward_id                       = _global_state->new_reward_id();
            reward.total_rewards                   += total_rewards;
            reward.last_rewards                    = total_rewards;
            reward.unalloted_rewards               += total_rewards;
            reward.last_reward_per_share           = reward.reward_per_share;
            reward.reward_per_share                = reward.reward_per_share + calc_reward_per_share_delta(total_rewards, pool_itr->avl_principal);
            reward.prev_reward_added_at            = reward.reward_added_at;
            reward.reward_added_at                 = current_time_point();
            c.airdrop_rewards[ total_rewards.symbol ] = reward;
            
         }
   });
}

void tyche_earn::refuelreward_to_all( const name& token_bank, const asset& total_rewards, const uint64_t& seconds){
   
   auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
   auto reward_symbol      = reward_symbols.find( total_rewards.symbol.code().raw() );
   CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "reward symbol not found:" + total_rewards.to_string()  )
   CHECKC( token_bank == reward_symbol->sym.get_contract(), err::RECORD_NOT_FOUND, "bank not equal" )
   CHECKC( reward_symbol->on_shelf, err::RECORD_NOT_FOUND, "reward_symbol not on_shelf" )

   auto now             = time_point_sec(current_time_point());
   uint64_t total_share = 0;
   auto pools           = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr        = pools.begin();
   CHECKC( pool_itr != pools.end(), err::RECORD_NOT_FOUND, "save plan not found" )
   while( pool_itr != pools.end() ) {
      if( pool_itr->on_shelf ) {
          total_share += pool_itr->share_multiplier * pool_itr->avl_principal.amount;
      }
      pool_itr++;
   }
   CHECKC( total_share > 0, err::INCORRECT_AMOUNT, "total share is not positive: " + to_string(total_share) )
   
   pool_itr             = pools.begin();
   while( pool_itr      != pools.end() ){
      if( !pool_itr->on_shelf ) { pool_itr++; continue; }

      auto rate         = pool_itr->avl_principal.amount * pool_itr->share_multiplier * PCT_BOOST / total_share;
      auto rewards      = asset(total_rewards.amount * rate / PCT_BOOST, total_rewards.symbol);
      
      auto new_reward_id                      = _global_state->new_reward_id();
      if(pool_itr->airdrop_rewards.count(total_rewards.symbol) == 0) {
         pools.modify( pool_itr, _self, [&]( auto& c ) {
            auto reward = earn_pool_reward_st();
            reward.reward_id                  = new_reward_id;
            reward.total_rewards              = rewards;
            reward.last_rewards               = rewards;
            reward.unalloted_rewards          = rewards;
            reward.unclaimed_rewards          = asset(0, total_rewards.symbol);
            reward.claimed_rewards            = asset(0, total_rewards.symbol);
            reward.last_reward_per_share      = 0; 
            reward.reward_per_share           = calc_reward_per_share_delta(rewards, pool_itr->avl_principal);
            reward.reward_added_at            = now;
            
            c.airdrop_rewards[total_rewards.symbol] = reward;
         });
         
      } else if( rewards.amount > 0) {
         pools.modify( pool_itr, _self, [&]( auto& c ) {
            auto reward                         = pool_itr->airdrop_rewards.at(rewards.symbol);
            reward.reward_id                    = new_reward_id;
            reward.total_rewards                += rewards;
            reward.last_rewards                 = rewards;
            reward.unalloted_rewards            += rewards;
            reward.last_reward_per_share        = reward.reward_per_share;
            reward.reward_per_share             = reward.reward_per_share + calc_reward_per_share_delta(rewards, pool_itr->avl_principal);
            reward.prev_reward_added_at         = reward.reward_added_at;
            reward.reward_added_at              = now;

            c.airdrop_rewards[total_rewards.symbol] = reward;
         });
      }
      pool_itr++;
   }
}

void tyche_earn::ondeposit( const name& from, const uint64_t& term_code, const asset& quant ){
   CHECKC( quant.symbol == _gstate.principal_token.get_symbol(), err::SYMBOL_MISMATCH, "symbol mismatch" )
   CHECKC( _gstate.min_deposit_amount <= quant, err::INCORRECT_AMOUNT, "deposit amount too small" )
   auto now = time_point_sec(current_time_point());
   CHECKC( _gstate.enabled, err::PAUSED, "not effective yet" )

   auto pools              = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr           = pools.find( term_code );
   CHECKC( pool_itr != pools.end(), err::RECORD_NOT_FOUND, "earn pool not found" )

   auto accts              = earner_t::tbl_t(_self, term_code);
   auto acct               = accts.find( from.value );
   if( acct == accts.end() ) {
      pools.modify( pool_itr, _self, [&]( auto& c ) {
         c.cum_principal            += quant;
         c.avl_principal            += quant;
      });

      acct = accts.emplace( _self, [&]( auto& a ) {
         a.owner                 = from;
         a.avl_principal         = quant;
         a.cum_principal         = quant;
         a.interest_reward       = _get_new_shared_earner_reward( pool_itr->interest_reward);
         a.airdrop_rewards       = _get_new_shared_earner_reward_map( pool_itr->airdrop_rewards);
         a.term_started_at       = now;
         a.term_ended_at         = now + pool_itr->term_interval_sec;
         a.created_at            = now;
      });

   } else {
      //当用户充入本金, 要结算充入池子的用户之前的利息，同时要修改充入池子的基本信息
      //循环结算每一种利息代币
      earn_pool_reward_map pool_airdrop_rewards       = pool_itr->airdrop_rewards;
      earner_reward_map    earner_airdrop_rewards     = acct->airdrop_rewards;
      auto older_deposit_quant                        = acct->avl_principal;
      //结算奖励
      for (auto& pool_airdrop_rewards_kv : pool_itr->airdrop_rewards) { //for循环每一个token
         auto pool_airdrop_reward   = pool_airdrop_rewards_kv.second;
         auto code                  = pool_airdrop_rewards_kv.first;
         auto earner_airdrop_reward = earner_reward_st();
         auto new_rewards           = asset(0, pool_airdrop_reward.total_rewards.symbol);
         //初始化earner_reward 信息
         if(acct->airdrop_rewards.count(code)) {
            earner_airdrop_reward   = acct->airdrop_rewards.at(code);
         } else {
            earner_airdrop_reward.unclaimed_rewards         = asset(0, pool_airdrop_reward.total_rewards.symbol);
            earner_airdrop_reward.claimed_rewards           = asset(0, pool_airdrop_reward.total_rewards.symbol);
            earner_airdrop_reward.total_claimed_rewards     = asset(0, pool_airdrop_reward.total_rewards.symbol);
            earner_airdrop_reward.last_reward_per_share     = 0; //如果没有，说明此用户在奖励前质押的
         }

         int128_t reward_per_share_delta = pool_airdrop_reward.reward_per_share - earner_airdrop_reward.last_reward_per_share;
         if ( reward_per_share_delta > 0 ) {
            new_rewards = calc_sharer_rewards(older_deposit_quant, reward_per_share_delta, pool_airdrop_reward.total_rewards.symbol);
            // CHECKC(false, err::ACCOUNT_INVALID, "rewards error: " + new_rewards.to_string())
            pool_airdrop_reward.unalloted_rewards          -= new_rewards;
            pool_airdrop_reward.unclaimed_rewards          += new_rewards;
            earner_airdrop_reward.last_reward_per_share    = pool_airdrop_reward.reward_per_share;
            earner_airdrop_reward.unclaimed_rewards        += new_rewards;
         }
         pool_airdrop_rewards[code]                         = pool_airdrop_reward;
         earner_airdrop_rewards[code]                       = earner_airdrop_reward;
      }

      auto earner_interest_reward                           = acct->interest_reward;
      auto pool_interest_reward                             = pool_itr->interest_reward;
      //结算利息
      {
         int128_t reward_per_share_delta              = pool_interest_reward.reward_per_share - earner_interest_reward.last_reward_per_share;
         auto new_rewards                             = calc_sharer_rewards(older_deposit_quant, reward_per_share_delta, pool_interest_reward.total_rewards.symbol);
         CHECKC(new_rewards.amount >= 0, err::INCORRECT_AMOUNT,  "new reward must be positive")
         pool_interest_reward.unalloted_rewards       -= new_rewards;
         pool_interest_reward.unclaimed_rewards       += new_rewards;
         earner_interest_reward.last_reward_per_share = pool_interest_reward.reward_per_share;
         earner_interest_reward.unclaimed_rewards     += new_rewards;
      }
   
      pools.modify( pool_itr, _self, [&]( auto& c ) {
         c.cum_principal               += quant;
         c.avl_principal               += quant;
         c.interest_reward             = pool_interest_reward;
         c.airdrop_rewards             = pool_airdrop_rewards;
      });

      accts.modify( acct, _self,  [&]( auto& c ) {
         if( c.avl_principal.amount == 0 ) {
            c.created_at               = now;
         }
         c.cum_principal               += quant;
         c.avl_principal               += quant;
         c.airdrop_rewards             = earner_airdrop_rewards;
         c.interest_reward             = earner_interest_reward;
         c.term_started_at             = now;
         c.term_ended_at               = now + pool_itr->term_interval_sec;
      });
   }
   //transfer nusdt to earner
   TRANSFER( _gstate.lp_token.get_contract(), from, asset(quant.amount, _gstate.lp_token.get_symbol()), "deposit credential:" + to_string(term_code)  )
   //只有天池5号才有奖励
   if(term_code == _gstate.tyche_reward_pool_code) {
      //打出TYCHE
      auto tyche_amount = quant.amount * _gstate.tyche_farm_ratio / PCT_BOOST * (get_precision(TYCHE)/get_precision(quant.symbol));
      // CHECKC(false, err::ACCOUNT_INVALID, "test errror:" + asset(tyche_amount, TYCHE).to_string())
      TRANSFER( TYCHE_BANK, from, asset(tyche_amount, TYCHE), "tyche farm reward:" + to_string(term_code))
      _apl_reward(from, quant,term_code);
   }
}

//用户提款只能按全额来提款
void tyche_earn::onredeem( const name& from, const uint64_t& term_code, const asset& quant ){
   auto pools        = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr     = pools.find( term_code );
   CHECKC( pool_itr != pools.end(), err::RECORD_NOT_FOUND, "earn pool not found" )

   auto accts        = earner_t::tbl_t(_self, term_code);
   auto acct         = accts.find( from.value );
   CHECKC( acct != accts.end(), err::RECORD_NOT_FOUND, "account not found" )

   auto now          = current_time_point();
   CHECKC(acct->avl_principal.amount !=0, err::PLAN_INEFFECTIVE, "already redeemed" )
   CHECKC(acct->avl_principal.amount == quant.amount, err::INCORRECT_AMOUNT, "insufficient deposit amount" )
   CHECKC(acct->term_ended_at <= now, err::TIME_PREMATURE, "premature to redeedm" )
   
   _claim_pool_rewards(from, term_code, true);

   //打出本金MUSDT
   TRANSFER( MUSDT_BANK, from, asset(quant.amount, MUSDT), "redeem:" + to_string(term_code) )
   if(term_code == _gstate.tyche_reward_pool_code) {
      //打出TYCHE
      auto tyche_amount = quant.amount * _gstate.tyche_farm_lock_ratio / PCT_BOOST * (get_precision(TYCHE)/get_precision(acct->avl_principal));
      TRANSFER( TYCHE_BANK, from, asset(tyche_amount, TYCHE), "redeem:" + to_string(term_code) )
   }
}

void tyche_earn::addrewardsym(const extended_symbol& sym) {
   require_auth(_self);
   auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
   auto reward_symbol      = reward_symbols.find( sym.get_symbol().code().raw() );
   CHECKC( reward_symbol == reward_symbols.end(), err::RECORD_EXISTING, "save plan not found" )
   reward_symbols.emplace( _self, [&]( auto& s ) {
      s.sym                         = sym;
      s.on_shelf                    = true;
   });
}
//

void tyche_earn::setmindepamt(const asset& quant) {
   require_auth(_self);
   CHECKC(quant.symbol== _gstate.min_deposit_amount.symbol, err::MEMO_FORMAT_ERROR, "symbol error")
   _gstate.min_deposit_amount      = quant;
}

void tyche_earn::claimrewards(const name& from){
   require_auth(from);

   auto pools        = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr     = pools.begin();
   bool finalclaimed = false;
   while( pool_itr != pools.end() ) {
      if( !pool_itr->on_shelf ) { pool_itr++; continue; }
      auto claimed   = _claim_pool_rewards(from, pool_itr->code, false);
      if (!finalclaimed) 
         finalclaimed = claimed;

      pool_itr++;
   }
   CHECKC(finalclaimed, err::RECORD_NOT_FOUND, "no reward to claim for " + from.to_string() )
}

bool tyche_earn::_claim_pool_rewards(const name& from, const uint64_t& term_code, const bool& term_end_flag ){
   auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
   bool existed            = false;

   auto now                = time_point_sec(current_time_point());
   auto pools              = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr           = pools.find( term_code );
   CHECKC( pool_itr != pools.end(), err::RECORD_NOT_FOUND, "earn pool not found" )

   auto accts  = earner_t::tbl_t(_self, term_code);
   auto acct   = accts.find( from.value );
   if(acct == accts.end())
      return false;

   auto reward_symbol_ptr      = reward_symbols.begin();
   auto earner_airdrop_rewards   = acct->airdrop_rewards;
   auto pool_airdrop_rewards     = pool_itr->airdrop_rewards;
   while(reward_symbol_ptr != reward_symbols.end()) {
      if(!reward_symbol_ptr->on_shelf) {reward_symbol_ptr++; continue;}
      auto sym    = reward_symbol_ptr->sym.get_symbol();
      if( pool_itr->airdrop_rewards.count( sym ) == 0 ) {
         reward_symbol_ptr++;
         continue;
      }
      earner_reward_st earner_airdrop_reward = {0, asset(0, sym), asset(0,sym), asset(0, sym)};
      if(acct->airdrop_rewards.count( sym ) > 0) {
         earner_airdrop_reward   = acct->airdrop_rewards.at(sym);
      }
      auto pool_airdrop_reward     = pool_itr->airdrop_rewards.at(sym);


      auto total_rewards            = _update_reward_info(pool_airdrop_reward, earner_airdrop_reward, acct->avl_principal, term_end_flag);
      earner_airdrop_rewards[sym]  = earner_airdrop_reward;
      pool_airdrop_rewards[sym]    = pool_airdrop_reward;
      //内部调用发放利息
      //发放利息
      if(total_rewards.amount > 0) {
         tyche_reward::claimreward_action claim_reward_act(_gstate.reward_contract, { {get_self(), "active"_n} });
         claim_reward_act.send(from, reward_symbol_ptr->sym.get_contract(), total_rewards, "reward:" + to_string(term_code));
         existed = true;
      }
      reward_symbol_ptr++;
   }

   auto pool_interest_reward     = pool_itr->interest_reward;
   auto eraner_interest_reward   = acct->interest_reward;
   {
      auto total_rewards         = _update_reward_info(pool_interest_reward, eraner_interest_reward, acct->avl_principal, term_end_flag);
      //内部调用发放利息
      //发放利息
      if(total_rewards.amount > 0) {
         tyche_reward::claimintr_action cliam_interest_act(_gstate.reward_contract, { {get_self(), "active"_n} });
         cliam_interest_act.send(from, MUSDT_BANK, total_rewards, "interest:" + to_string(term_code));
         existed = true;
      }
   }


   pools.modify( pool_itr, _self, [&]( auto& c ) {
      c.interest_reward                = pool_interest_reward;
      if( term_end_flag )           
         c.avl_principal.amount        -= acct->avl_principal.amount;
      c.airdrop_rewards                = pool_airdrop_rewards;
   });

   accts.modify( acct, _self, [&]( auto& a ) {
      a.interest_reward                = eraner_interest_reward; 
      if( term_end_flag ) {
         a.avl_principal.amount        = 0;
      }
         
      a.airdrop_rewards                = earner_airdrop_rewards;
   });
   return existed;
}

//领取奖励,返回要领取的奖励
asset tyche_earn::_update_reward_info( earn_pool_reward_st& pool_reward, earner_reward_st& earner_reward, const asset& earner_avl_principal, const bool& term_end_flag) {
   int128_t reward_per_share_delta = pool_reward.reward_per_share - earner_reward.last_reward_per_share;

   auto new_rewards        = calc_sharer_rewards(earner_avl_principal, reward_per_share_delta, pool_reward.total_rewards.symbol);
   auto total_rewards      = new_rewards + earner_reward.unclaimed_rewards;
   // CHECKC(false, err::INCORRECT_AMOUNT, "new_rewards must be greater than zero:" + new_rewards.to_string())

   pool_reward.unalloted_rewards          -= new_rewards;
   pool_reward.unclaimed_rewards          -= earner_reward.unclaimed_rewards;
   pool_reward.claimed_rewards            += total_rewards;

   earner_reward.unclaimed_rewards        = asset(0, total_rewards.symbol);
   if( term_end_flag ){
      earner_reward.claimed_rewards       = asset(0, total_rewards.symbol);
   } else {
      earner_reward.claimed_rewards       += total_rewards;
   }
   // CHECKC(false, err::INCORRECT_AMOUNT, "new_rewards must be greater than zero3:" + new_rewards.to_string() + "total_rewards:" + total_rewards.to_string() + ",total_claimed_rewards: " 
   //          + earner_reward.total_claimed_rewards.to_string() )

   earner_reward.total_claimed_rewards    += total_rewards;
   earner_reward.last_reward_per_share    = pool_reward.reward_per_share;
   return total_rewards;
}

earner_reward_map tyche_earn::_get_new_shared_earner_reward_map(const earn_pool_reward_map& rewards) {
   earner_reward_map airdrop_rewards;
   for (auto& pool_airdrop_reward : rewards) {
      airdrop_rewards[pool_airdrop_reward.first]   = _get_new_shared_earner_reward(pool_airdrop_reward.second);
   }
   return airdrop_rewards;
}

void tyche_earn::createpool(const uint64_t& code, const uint64_t& term_interval_sec, const uint64_t& share_multiplier) {
   require_auth(_self);
   auto pools              = earn_pool_t::tbl_t(_self, _self.value);
   auto pool_itr           = pools.find( code );
   if( pool_itr == pools.end() ) {
      pools.emplace( _self, [&]( auto& c ) {
         c.code                  = code;
         c.term_interval_sec     = term_interval_sec;
         c.share_multiplier      = share_multiplier;
         c.interest_reward       = _init_interest_conf();
         c.on_shelf              = true;
         c.created_at            = time_point_sec(current_time_point());
      });
   } else {
      pools.modify( pool_itr, _self, [&]( auto& c ) {
         c.term_interval_sec     = term_interval_sec;
         c.share_multiplier      = share_multiplier;
         c.on_shelf              = true;
      });
   }
}

void tyche_earn::onshelfsym(const extended_symbol& sym, const bool& on_shelf) {
   require_auth(_self);
   auto reward_symbols     = reward_symbol_t::idx_t(_self, _self.value);
   auto reward_symbol      = reward_symbols.find( sym.get_symbol().code().raw() );
   CHECKC( reward_symbol != reward_symbols.end(), err::RECORD_NOT_FOUND, "reward symbol not found" )
   reward_symbols.modify( reward_symbol, _self, [&]( auto& s ) {
      s.on_shelf               = on_shelf;
   });
}

earn_pool_reward_st tyche_earn::_init_interest_conf(){
   auto conf_id = _global_state->new_reward_id();
   earn_pool_reward_st pool_reward = {conf_id, 
                     asset(0, MUSDT),
                     asset(0, MUSDT),
                     asset(0, MUSDT), 
                     asset(0, MUSDT), 
                     asset(0, MUSDT), 
                     0,
                     0, 
                     time_point_sec(current_time_point()), 
                     time_point_sec(current_time_point())};
   return pool_reward;
}
//init earner_reward first time 
earner_reward_st tyche_earn::_get_new_shared_earner_reward(const earn_pool_reward_st& pool_reward) {
   earner_reward_st earner_reward;
   earner_reward.last_reward_per_share = pool_reward.reward_per_share;
   earner_reward.unclaimed_rewards     = asset(0, pool_reward.total_rewards.symbol);
   earner_reward.claimed_rewards       = asset(0, pool_reward.total_rewards.symbol);
   earner_reward.total_claimed_rewards = asset(0, pool_reward.total_rewards.symbol);
   return earner_reward;
}

void tyche_earn::_apl_reward(const name& from, const asset& quant, const uint64_t& term_code) {

   rewardglobal_t::table reward_global(_gstate.reward_contract, _gstate.reward_contract.value);
   auto reward_gstate = reward_global.get();

   auto apls_amount = quant.amount/get_precision(quant) * 
            reward_gstate.annual_interest_rate * _gstate.apl_farm.unit_reward.amount / PCT_BOOST;

   if(apls_amount > 0) {
      ALLOT_APPLE( _gstate.apl_farm.contract, _gstate.apl_farm.lease_id, from, asset(apls_amount, APLINK_SYMBOL), "tyche earn reward:" + to_string(term_code))
   }
}

void tyche_earn::setaplconf( const uint64_t& lease_id, const asset& unit_reward ){
   require_auth(_self);
   _gstate.apl_farm.lease_id     = lease_id;
   _gstate.apl_farm.unit_reward  = unit_reward;
}

}