#pragma once

namespace wasm { namespace safemath {
    template<typename T>
    uint128_t divide_decimal(uint128_t a, uint128_t b, T precision) {
        uint128_t tmp = 10 * a * precision  / b;
        return (tmp + 5) / 10;
    }

    template<typename T>
    uint128_t multiply_decimal_up(uint128_t a, uint128_t b, T precision) {
        uint128_t tmp = 10 * a * b / precision;
        return (tmp + 5) / 10;
    }

    template<typename T>
    uint128_t multiply_decimal_down(uint128_t a, uint128_t b, T precision) {
        uint128_t tmp = 10 * a * b / precision;
        return tmp / 10;
    }


    #define div(a, b, p) divide_decimal(a, b, p)
    #define mul_up(a, b, p) multiply_decimal_up(a, b, p)
    #define mul_down(a, b, p) multiply_decimal_down(a, b, p)

} } //safemath
