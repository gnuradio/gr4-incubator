#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>


namespace gr::analog {

GR_REGISTER_BLOCK("Quadrature Demod Block", gr::analog::QuadratureDemod, ([T]), [ uint8_t, int16_t, int32_t ])

template<typename T>
struct QuadratureDemod : Block<QuadratureDemod<T>> {

    using Description = Doc<"@brief Copies from input to output.">;

    PortIn<std::complex<T>> in;
    PortOut<T> out;

    double gain{1.0};

    GR_MAKE_REFLECTABLE(QuadratureDemod, in, out, gain);

    std::complex<T> lastValue{std::complex<T>(0.0)};

    [[nodiscard]] constexpr T processOne(std::complex<T> input) noexcept { 
        auto tmp = lastValue * conj(input);
        lastValue = input;
        return gain * atan2(imag(tmp), real(tmp));
    }
};

} // namespace gr::analog

