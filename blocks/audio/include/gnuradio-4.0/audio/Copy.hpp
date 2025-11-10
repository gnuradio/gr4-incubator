#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>


namespace gr::audio {

template<typename T>
struct Copy : Block<Copy<T>> {

    using Description = Doc<"@brief Copies from input to output.">;

    PortIn<T> in;
    PortOut<T> out;

    GR_MAKE_REFLECTABLE(Copy, in, out);

    [[nodiscard]] constexpr T processOne(T input) const noexcept { return input; }
};

} // namespace gr::audio

GR_REGISTER_BLOCK(gr::audio::Copy, [ uint8_t, int16_t, int32_t ])