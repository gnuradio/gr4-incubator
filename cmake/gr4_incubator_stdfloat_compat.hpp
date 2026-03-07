#pragma once

#include <limits>

#if __has_include(<stdfloat>) && !defined(__ADAPTIVECPP__)
#include <stdfloat>
#endif

#if !defined(__STDCPP_FLOAT32_T__) || !defined(__STDCPP_FLOAT64_T__)
// Match GNURadio4 fallback mechanics when C++23 stdfloat typedefs are missing.
namespace std {
using float32_t = float;
using float64_t = double;

static_assert(std::numeric_limits<float32_t>::is_iec559 && sizeof(float32_t) * 8 == 32, "float32_t must be a 32-bit IEEE 754 float");
static_assert(std::numeric_limits<float64_t>::is_iec559 && sizeof(float64_t) * 8 == 64, "float64_t must be a 64-bit IEEE 754 double");
} // namespace std
#endif
