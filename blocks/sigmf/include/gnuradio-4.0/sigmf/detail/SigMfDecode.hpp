#pragma once

#include <bit>
#include <complex>
#include <cstdint>

#include <gnuradio-4.0/sigmf/detail/SigMfDatatype.hpp>

namespace gr::incubator::sigmf::detail {

// This stays intentionally narrow for v1: only little-endian rf32_le/cf32_le records are decoded here.
[[nodiscard]] inline float decodeFloat32Le(const std::uint8_t* bytes) {
    std::uint32_t bits = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if constexpr (std::endian::native == std::endian::big) {
        bits = std::byteswap(bits);
    }
    return std::bit_cast<float>(bits);
}

template<typename T>
[[nodiscard]] inline T decodeSigMfSample(const std::uint8_t* bytes) {
    if constexpr (std::same_as<T, float>) {
        return decodeFloat32Le(bytes);
    } else {
        return T{decodeFloat32Le(bytes), decodeFloat32Le(bytes + sizeof(float))};
    }
}

} // namespace gr::incubator::sigmf::detail
