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

   inline void _term_interest( const uint64_t interest_rate, const asset& deposit_quant, 
                              const uint64_t real_duration, const uint64_t& total_duraton, asset& interest ) {
      CHECKC ( real_duration > 0, err::PARAM_ERROR, "invald param ") 
      interest.amount = mul_down( mul_down(interest_rate * 100, real_duration, total_duraton), deposit_quant.amount, PCT_BOOST * 100 );
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
    */
   void usdt_save::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
      CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );

      // check(false, "under maintenance");
      
      if (from == get_self() || to != get_self()) return;
      auto token_bank = get_first_receiver();
      if(quant.symbol == _gstate.principal_token.symbol) {
         CHECKC( _gstate.principal_token.get_contract() == token_bank, err::CONTRACT_MISMATCH, "interest token contract mismatches" )
         ondeposit(from, quant, memo);
      } else if(quant.symbol == _gstate.voucher_token.symbol) { //redeem
         CHECKC( _gstate.voucher_token.get_contract() == token_bank, err::CONTRACT_MISMATCH, "interest token contract mismatches" )
         onredeem(from, quant, memo);
      }
   }

   void usdt_save::ondeposit(const name& from, const asset& quant, const string& memo ){
      if( memo == "refuel" ) {
         _gstate.total_interest_quant  += quant;
         _gstate.remain_interest_quant += quant;
      } else {
         //deposit
         CHECKC( _gstate.mini_deposit_amount <= quant, err::INCORRECT_AMOUNT, "deposit amount too small" )
         auto now = time_point_sec(current_time_point());
         CHECKC( _gstate.enabled, err::PLAN_INEFFECTIVE, "not effective yet" )

         auto accts              = save_account_t::tbl_t(_self, _self.value);
         auto acct               = accts.find( from.value );
         auto now                = current_time_point();

         if( acct == accts.end() ) {
            acct = accts.emplace( _self, [&]( auto& a ) {
               a.account                     = from;
               a.deposit_quant               = quant;
               a.total_deposit_quant         = quant;
               a.total_interest_redemption   = asset(0, quant.symbol);
               a.interest_collected          = asset(0, quant.symbol);
               a.created_at                  = now();
               a.last_interest_settled_at    = now();
            });
         } else {
            //结算利息
            auto total_elapsed_sec  = now.sec_since_epoch() - save_acct.created_at.sec_since_epoch();
            auto interest           = asset( 0, _gstate.interest_token.get_symbol() );
            _term_interest( _gstate.interest_rate, quant, total_elapsed_sec, YEAR_DAYS * DAY_SECONDS,interest );
            accts.modify( acct, _self, [&]( auto& a ) {
               a.deposit_quant               += quant;
               a.total_deposit_quant         += quant;
               a.interest_collected          += interest;
               a.last_interest_settled_at    = now();
            });
         }

         _gstate.total_deposit_quant        += quant;
         _gstate.remain_deposit_quant       += quant;
         //transfer nusdt to user
         auto nusdt_quant =  asset(quant.amount, _gstate.voucher_token.get_symbol());
      }
   }

   void usdt_save::onredeem(const name& from, const asset& quant, const string& memo ){
      auto accts              = save_account_t::tbl_t(_self, _self.value);
      auto acct               = accts.find( from.value );
      CHECKC( acct != accts.end(), err::RECORD_NOT_FOUND, "account not found" )
      auto now                = current_time_point();
      CHECKC(acct.deposit_quant.amount >= quant.amount, err::INCORRECT_AMOUNT, "insufficient deposit amount" )
      auto total_elapsed_sec  = now.sec_since_epoch() - save_acct.created_at.sec_since_epoch();
      auto interest           = asset( 0, _gstate.principal_token.get_symbol() );
      _term_interest( _gstate.interest_rate, quant, total_elapsed_sec, YEAR_DAYS * DAY_SECONDS, interest );

      auto total_interest = acct.interest_collected + interest;
      auto rate = get_rate(acct.total_deposit_quant.amount, quant.amount);
      auto redeem_interest = asset(total_interest.amount * rate / PCT_BOOST, total_interest.symbol);
      auto remain_interest = total_interest - redeem_interest;
      
      accts.modify( acct, _self, [&]( auto& a ) {
         a.deposit_quant.amount        -= quant.amount;
         a.interest_collected          = remain_interest;
         a.last_interest_settled_at    = now;
         a.total_interest_redemption   += redeem_interest;
      });
      //transfer MUSDT to user
      auto total_quant = asset(quant.amount + redeem_interest.amount, redeem_interest.symbol);
   }

   uint64_t usdt_save::get_rate(const uint64_t& total_amount, const uint64_t& current_amount) {
      return (current_amount * PCT_BOOST) / (total_amount ) ;
   }

   void usdt_save::apl_reward(const asset& interest) {
   
   }
} //namespace amax