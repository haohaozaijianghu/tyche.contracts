#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/privileged.hpp>

#include <string>

#define PP(prop) "," #prop ":", prop
#define PP0(prop) #prop ":", prop
#define PRINT_PROPERTIES(...) eosio::print("{", __VA_ARGS__, "}")

#define CHECK(exp, msg) { if (!(exp)) eosio::check(false, msg); }

#ifndef ASSERT
    #define ASSERT(exp) CHECK(exp, #exp)
#endif

namespace entu {

   using std::string;
   using eosio::contract;
   using eosio::name;
   using eosio::asset;
   using eosio::symbol;
   using eosio::block_timestamp;

   static constexpr name      SYSTEM_CONTRACT   = "amax"_n;
   static constexpr symbol    vote_symbol       = symbol("VOTE", 4);
   static const asset         vote_asset_0      = asset(0, vote_symbol);
   static constexpr symbol    entu_symbol       = symbol("ENTU", 8);
   static constexpr name      ENTU_TOKEN        = "amax.token"_n;

   static constexpr int128_t  HIGH_PRECISION    = 1'000'000'000'000'000'000; // 10^18


   #define ENTU_ASSET(amount) asset(amount, entu_symbol)
   
   /**
    * The `entu.reward` contract is used as a reward dispatcher contract for amax.system contract.
    *
    */
   class [[eosio::contract("entu.reward")]] entu_reward : public contract {
      public:
         entu_reward( name s, name code, eosio::datastream<const char*> ds ):
               contract(s, code, ds),
               _global(get_self(), get_self().value),
               _voter_tbl(get_self(), get_self().value),
               _gvote_tbl(get_self(), get_self().value),
         {
            _gstate  = _global.exists() ? _global.get() : global_state{};
            _gvote  = _gvote_tbl.exists() ? _gvote_tbl.get() : global_savevote{};
         }

         /**
          * addvote.
          *
          * @param voter      - the account of voter,
          * @param votes      - votes value,
          */
         [[eosio::action]]
         void addvote( const name& voter, const asset& votes );

         /**
          * subvote.
          *
          * @param voter      - the account of voter,
          * @param votes      - votes value,
          */
         [[eosio::action]]
         void subvote( const name& voter, const asset& votes );

         /**
          * claim rewards for voter
          *
          * @param voter_name - the account of voter
          */
         [[eosio::action]]
         void claimrewards( const name& voter_name );

        /**
         * Notify by transfer() of xtoken contract
         *
         * @param from - the account to transfer from,
         * @param to - the account to be transferred to,
         * @param quantity - the quantity of tokens to be transferred,
         * @param memo - the memo string to accompany the transaction.
         */
        [[eosio::on_notify("amax.token::transfer")]]
        void ontransfer(   const name &from,
                           const name &to,
                           const asset &quantity,
                           const string &memo);

         using addvote_action = eosio::action_wrapper<"addvote"_n, &entu_reward::addvote>;
         using subvote_action = eosio::action_wrapper<"subvote"_n, &entu_reward::subvote>;
         using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &entu_reward::claimrewards>;
   public:
         struct [[eosio::table("global")]] global_state {
            asset                total_rewards  = ENTU_ASSET(0);

            typedef eosio::singleton< "global"_n, global_state >   table;
         };

         /**
          * producer table.
          * scope: contract self
         */
         struct [[eosio::table]] global_savevote {
            asset             total_rewards        = ENTU_ASSET(0);  // 现在还剩余的奖励
            asset             allocating_rewards   = ENTU_ASSET(0);  // 正在分配的奖励
            asset             allocated_rewards    = ENTU_ASSET(0);  // = total_rewards - allocating_rewards
            asset             votes                = vote_asset_0;   // 当前投票数
            int128_t          rewards_per_vote     = 0;              // 每票奖励
            block_timestamp   update_at;

            typedef eosio::multi_index< "globalvote"_n, global_savevote > table;
         };

         struct voted_producer_info {
            int128_t           last_rewards_per_vote         = 0;
         };

         using voted_producer_map = std::map<name, voted_producer_info>;

         /**
          * voter table.
          * scope: contract self
         */
         struct [[eosio::table]] voter {
            name                       owner;
            asset                      votes                   = vote_asset_0;
            int128_t                   last_rewards_per_vote   = 0;
            asset                      unclaimed_rewards       = ENTU_ASSET(0);
            asset                      claimed_rewards         = ENTU_ASSET(0);
            block_timestamp            update_at;

            uint64_t primary_key()const { return owner.value; }

            typedef eosio::multi_index< "voters"_n, voter > table;
         };

   private:
      global_state::table     _global;
      global_state            _gstate;
      global_savevote::table  _gvote_tbl;
      global_savevote         _gvote;

      voter::table            _voter_tbl;

      void allocate_rewards(int128_t &last_rewards_per_vote, const asset& votes_old, const asset& votes_delta, const name& new_payer, asset &allocated_rewards_out);
      void change_vote(const name& voter, const asset& votes, bool is_adding);
   };

}
