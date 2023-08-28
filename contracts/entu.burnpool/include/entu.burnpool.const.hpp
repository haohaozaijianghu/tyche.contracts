#pragma once

#include <cstdint>
#include <eosio/name.hpp>
#include <eosio/asset.hpp>

using namespace eosio;

#define SYMBOL(sym_code, precision) symbol(symbol_code(sym_code), precision)

enum class err: uint32_t {
   NONE                 = 0,

   NO_AUTH              = 10001,
   ACCOUNT_INVALID      = 10002,
   OVERSIZED            = 10003,
   NOT_POSITIVE         = 10004,

   RECORD_FOUND         = 10008,
   RECORD_NOT_FOUND     = 10009,

   PARAM_ERROR          = 10101,
   MEMO_FORMAT_ERROR    = 10102,

   SYMBOL_MISMATCH      = 10201,
   SYMBOL_NOT_SUPPORTED = 10202,
   FEE_INSUFFICIENT     = 10203,
   RATE_EXCEEDED        = 10204,
   QUANTITY_INVALID     = 10205,

   NOT_STARTED          = 10300,
   PAUSED               = 10301,
   EXPIRED              = 10302,
   NOT_EXPIRED          = 10303,
   STATE_MISMATCH       = 10304,

   SYSTEM_ERROR         = 20000
};

#define ENTU_BURNPOOL_PROP eosio::contract("entu.burnpool")
#define ENTU_BURNPOOL  [[ENTU_BURNPOOL_PROP]]
#define ENTU_TABLE struct [[eosio::table, ENTU_BURNPOOL_PROP]] 
#define ENTU_TABLE_NAME(name) [[eosio::table(name), ENTU_BURNPOOL_PROP]]
#define SYMBOL(sym_code, precision) symbol(symbol_code(sym_code), precision)


static constexpr name BLACK_HOLE_ACCOUNT  = name("oooo");
static constexpr name   AMAX_TOKEN         = name("amax.token");
static constexpr symbol ENTU_SYMBOL        = SYMBOL("ENTU", 4);


#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, std::string("$$$") + std::to_string((int)code) + std::string("$$$ ") + msg); }