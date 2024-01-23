#include <tyche.proxy/tyche.proxy.hpp>
#include <tyche.proxy/tyche.proxy.db.hpp>
#include <amax.token.hpp>

#include "safemath.hpp"
#include <utils.hpp>


static constexpr eosio::name active_permission{"active"_n};

#define TRANSFER(bank, to, quantity, memo) \
    {	token::transfer_action act{ bank, { {_self, active_perm} } };\
			act.send( _self, to, quantity , memo );}

namespace tychefi {

using namespace std;
using namespace wasm::safemath;

#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + to_string((int)code) + string("]] ") + msg); }

void tyche_proxy::init(const name& tyche_loan_contract,
             const name& tyche_earn_contract) {
   require_auth( _self );
   _gstate.tyche_loan_contract         = tyche_loan_contract;
   _gstate.tyche_earn_contract        = tyche_earn_contract;
   _gstate.enabled                     =true;
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
void tyche_proxy::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
   if(from == get_self() || to != get_self()) return;

   CHECKC( from != to, err::ACCOUNT_INVALID, "cannot transfer to self" );
   auto token_bank = get_first_receiver();
   CHECKC( token_bank == _gstate.token_bank, err::CONTRACT_MISMATCH, "invalid token contract" );
   CHECKC( quant.symbol == _gstate.loan_quant.symbol, err::SYMBOL_MISMATCH, "invalid symbol" );
   CHECKC( quant.amount > 0, err::NOT_POSITIVE, "must transfer positive quantity: "+  quant.to_string() );
   if(from == _gstate.tyche_loan_contract) {
      CHECKC(quant <= _gstate.loan_quant, err::NOT_POSITIVE, "transfer quantity must less than loan_quant: "+  _gstate.loan_quant.to_string() );
      _gstate.loan_quant += quant;
      TRANSFER(token_bank, _gstate.tyche_earn_contract, quant, "tyche_stake add" );
      return;
   }
   
   if(from == _gstate.tyche_earn_contract) {
      _gstate.loan_quant += quant;
      TRANSFER(token_bank, _gstate.tyche_loan_contract, quant, "tyche_proxy add" );
      return;
   }
   CHECKC(false, err::PARAM_ERROR, "from: " + from.to_string() + " to: " + to.to_string() + " quant: " + quant.to_string() + " memo: " + memo);
}

} //namespace tychefi