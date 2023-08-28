#include <entu.reward/entu.reward.hpp>
#include <eosio/system.hpp>

namespace entu {

using namespace eosio;

static constexpr name ACTIVE_PERM       = "active"_n;

struct amax_token {
      void transfer( const name&    from,
                     const name&    to,
                     const asset&   quantity,
                     const string&  memo );
      using transfer_action = eosio::action_wrapper<"transfer"_n, &amax_token::transfer>;
};

#define TRANSFER_OUT(token_contract, to, quantity, memo)                             \
            amax_token::transfer_action(token_contract, {{get_self(), ACTIVE_PERM}}) \
               .send(get_self(), to, quantity, memo);


namespace db {

    template<typename table, typename Lambda>
    inline void set(table &tbl,  typename table::const_iterator& itr, const eosio::name& emplaced_payer,
            const eosio::name& modified_payer, Lambda&& setter )
   {
        if (itr == tbl.end()) {
            tbl.emplace(emplaced_payer, [&]( auto& p ) {
               setter(p, true);
            });
        } else {
            tbl.modify(itr, modified_payer, [&]( auto& p ) {
               setter(p, false);
            });
        }
    }

    template<typename table, typename Lambda>
    inline void set(table &tbl,  typename table::const_iterator& itr, const eosio::name& emplaced_payer,
               Lambda&& setter )
   {
      set(tbl, itr, emplaced_payer, eosio::same_payer, setter);
   }

}// namespace db

inline static int128_t calc_rewards_per_vote(const int128_t& old_rewards_per_vote, const asset& rewards, const asset& votes) {
   ASSERT(rewards.amount >= 0 && votes.amount >= 0);
   int128_t  new_rewards_per_vote = old_rewards_per_vote;
   if (rewards.amount > 0 && votes.amount > 0) {
      new_rewards_per_vote = old_rewards_per_vote + rewards.amount * HIGH_PRECISION / votes.amount;
      CHECK(new_rewards_per_vote >= old_rewards_per_vote, "calculated rewards_per_vote overflow")
   }
   return new_rewards_per_vote;
}

inline static asset calc_voter_rewards(const asset& votes, const int128_t& rewards_per_vote) {
   ASSERT(votes.amount >= 0 && rewards_per_vote >= 0);
   CHECK(votes.amount * rewards_per_vote >= rewards_per_vote, "calculated rewards overflow");
   int128_t rewards = votes.amount * rewards_per_vote / HIGH_PRECISION;
   CHECK(rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow");
   return ENTU_ASSET((int64_t)rewards);
}

void entu_reward::addvote( const name& voter, const asset& votes ) {
   change_vote(voter, votes, true /* is_adding */);
}


void entu_reward::subvote( const name& voter, const asset& votes ) {
   change_vote(voter, votes, false /* is_adding */);
}

void entu_reward::change_vote(const name& voter, const asset& votes, bool is_adding) {
   require_auth( SYSTEM_CONTRACT );
   require_auth( voter );

   CHECK(votes.symbol == VOTE_SYMBOL, "votes symbol mismatch")
   CHECK(votes.amount > 0, "votes must be positive")

   auto now = eosio::current_time_point();
   auto voter_itr = _voter_tbl.find(voter.value);
   db::set(_voter_tbl, voter_itr, voter, voter, [&]( auto& v, bool is_new ) {
      if (is_new) {
         v.owner = voter;
      }

      auto votes_delta = votes;
      if (!is_adding) {
         CHECK(v.votes >= votes, "voter's votes insufficent")
         votes_delta = -votes;
      }
      allocate_rewards(v.last_rewards_per_vote, v.votes, votes_delta, voter, v.unclaimed_rewards);
      v.votes        += votes_delta;
      CHECK(v.votes.amount >= 0, "voter's votes can not be negtive")

      v.update_at    = now;
   });
}

void entu_reward::claimrewards(const name& voter) {
   require_auth( voter );

   auto now = eosio::current_time_point();
   auto voter_itr = _voter_tbl.find(voter.value);
   check(voter_itr != _voter_tbl.end(), "voter info not found");
   check(voter_itr->votes.amount > 0, "voter's votes must be positive");

   _voter_tbl.modify(voter_itr, voter, [&]( auto& v) {
      allocate_rewards(v.last_rewards_per_vote, v.votes, vote_asset_0, voter, v.unclaimed_rewards);
      check(v.unclaimed_rewards.amount > 0, "no rewards to claim");
      TRANSFER_OUT(ENTU_TOKEN, voter, v.unclaimed_rewards, "voted rewards");

      v.claimed_rewards += v.unclaimed_rewards;
      v.unclaimed_rewards.amount = 0;
      v.update_at = now;
   });
}

void entu_reward::ontransfer(    const name &from,
                                 const name &to,
                                 const asset &quantity,
                                 const string &memo)
{
   if (!(get_first_receiver() == ENTU_TOKEN && quantity.symbol == ENTU_SYMBOL && from != get_self() && to == get_self()))
      return;
   
   _gstate.total_rewards += quantity;
   _global.set(_gstate, get_self());

   _gvote.total_rewards         += quantity;
   _gvote.allocating_rewards    += quantity;
   _gvote.rewards_per_vote      = calc_rewards_per_vote(_gvote.rewards_per_vote, quantity, _gvote.votes);
   _gvote.update_at             = eosio::current_time_point();
   _gvote_tbl.set(_gvote, get_self());
}

void entu_reward::allocate_rewards(
         int128_t &last_rewards_per_vote,
         const asset& votes_old,
         const asset& votes_delta, 
         const name& new_payer, 
         asset &allocated_rewards_out ){

   auto now = eosio::current_time_point();
   CHECK(_gvote.rewards_per_vote >= last_rewards_per_vote, "last_rewards_per_vote invalid");
   int128_t rewards_per_vote_delta = _gvote.rewards_per_vote - last_rewards_per_vote;
   if (rewards_per_vote_delta > 0 && votes_old.amount > 0) {
      ASSERT(votes_old <= _gvote.votes)
      asset new_rewards = calc_voter_rewards(votes_old, rewards_per_vote_delta);
      CHECK(_gvote.allocating_rewards >= new_rewards, "producer allocating rewards insufficient");
      _gvote.allocating_rewards -= new_rewards;
      _gvote.allocated_rewards += new_rewards;
      ASSERT(_gvote.total_rewards == _gvote.allocating_rewards + _gvote.allocated_rewards)
      allocated_rewards_out += new_rewards; // update allocated_rewards for voter
   }

   _gvote.votes += votes_delta;
   CHECK(_gvote.votes.amount >= 0, "producer votes can not be negtive")
   _gvote.update_at = now;

   last_rewards_per_vote = _gvote.rewards_per_vote; // update for voted_prod

}

} /// namespace eosio
