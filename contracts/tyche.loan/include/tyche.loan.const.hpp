#pragma once

#include <cstdint>
#include <eosio/name.hpp>
#include <eosio/asset.hpp>
using namespace eosio;


static constexpr uint16_t  PCT_BOOST         = 10000;
static constexpr uint64_t  DAY_SECONDS       = 24 * 60 * 60;
static constexpr uint64_t  YEAR_SECONDS      = 24 * 60 * 60 * 365;
static constexpr uint64_t  YEAR_DAYS         = 365;
static constexpr int128_t  HIGH_PRECISION    = 1'000'000'000'000'000'000; // 10^18
static constexpr int64_t   RATIO_PRECISION   = 100000;       // 10^5, the ratio precision

