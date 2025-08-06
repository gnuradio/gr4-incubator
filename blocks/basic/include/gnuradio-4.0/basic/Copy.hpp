#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>



namespace gr::basic {

GR_REGISTER_BLOCK("Copy Block", gr::basic::Copy, ([T]), [ uint8_t, int16_t, int32_t ])

template<typename T>
struct Copy : Block<Copy<T>> {

    using Description = Doc<"@brief Copies from input to output.">;

    PortIn<T> in;
    PortOut<T> out;

    GR_MAKE_REFLECTABLE(Copy, in, out);

    [[nodiscard]] constexpr T processOne(T input) const noexcept { return input; }
};

} // namespace gr::basic

