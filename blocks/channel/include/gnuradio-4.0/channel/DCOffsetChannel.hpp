#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <complex>

namespace gr::incubator::channel {
using namespace gr;

template<typename T>
struct DCOffsetChannel : Block<DCOffsetChannel<T>> {
    using Description = Doc<
        "DC offset impairment channel. Adds a fixed complex bias to every sample, simulating "
        "local-oscillator (LO) leakage: y[n] = x[n] + (dc_i + j*dc_q). "
        "In direct-conversion (zero-IF) receivers the LO leaks through the mixer and creates "
        "a spike at DC. Even 1-2% LO feedthrough degrades 16-QAM and higher constellations significantly. "
        "Pairs with DCBlocker for a loopback impairment/correction demo. "
        "Signal chain: QAMModulator -> DCOffsetChannel -> DCBlocker -> QAMDemodulator -> BERSink.">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "dc_i", Visible, Doc<"I-channel (real) DC offset">> dc_i = T(0);
    Annotated<T, "dc_q", Visible, Doc<"Q-channel (imag) DC offset">> dc_q = T(0);

    GR_MAKE_REFLECTABLE(DCOffsetChannel, in, out, dc_i, dc_q);

    [[nodiscard]] constexpr std::complex<T> processOne(std::complex<T> x) const noexcept { return {x.real() + static_cast<T>(dc_i), x.imag() + static_cast<T>(dc_q)}; }
};

GR_REGISTER_BLOCK("gr::incubator::channel::DCOffsetChannel", gr::incubator::channel::DCOffsetChannel, ([T]), [ float, double ])

} // namespace gr::incubator::channel
