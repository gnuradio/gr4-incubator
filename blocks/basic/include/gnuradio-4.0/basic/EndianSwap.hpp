
#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <algorithm>
#include <bit>

namespace gr::incubator::basic {
using namespace gr;

GR_REGISTER_BLOCK("gr::incubator::basic::EndianSwap", gr::incubator::basic::EndianSwap, ([T]), [ uint8_t, int16_t, int32_t, float, double ])

template<typename T>
requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)
struct EndianSwap : Block<EndianSwap<T>> {
    using Description = Doc<"Reverses the byte order of each sample for endianness conversion between big-endian and little-endian. "
                            "No-op for sizeof(T)==1 (uint8_t). "
                            "Integers are byte-swapped via std::byteswap after casting to the unsigned equivalent. "
                            "Floating-point types (float, double) are byte-swapped using std::bit_cast to preserve the bit pattern. "
                            "Typical use: converting raw network or file data between host and wire byte order.">;

    PortIn<T>  in;
    PortOut<T> out;
    GR_MAKE_REFLECTABLE(EndianSwap, in, out);

    [[nodiscard]] T processOne(T x) const noexcept {
        if constexpr (sizeof(T) == 1) {
            return x;
        } else if constexpr (std::is_integral_v<T>) {
            return static_cast<T>(std::byteswap(static_cast<std::make_unsigned_t<T>>(x)));
        } else if constexpr (sizeof(T) == 4) {
            auto u = std::byteswap(std::bit_cast<uint32_t>(x));
            return std::bit_cast<T>(u);
        } else {
            auto u = std::byteswap(std::bit_cast<uint64_t>(x));
            return std::bit_cast<T>(u);
        }
    }
};

} // namespace gr::incubator::basic
