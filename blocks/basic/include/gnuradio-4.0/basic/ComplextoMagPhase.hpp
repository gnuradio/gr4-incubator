#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <complex>
#include <tuple>

namespace gr::incubator::basic {

GR_REGISTER_BLOCK("gr::incubator::basic::ComplexToMagPhase", gr::incubator::basic::ComplexToMagPhase, ([T]), [ float, double ])

template<typename T>
struct ComplexToMagPhase : Block<ComplexToMagPhase<T>> {

    using Description = Doc<"Splits a complex input stream into separate magnitude and phase streams. "
                            "For each input sample x: mag = |x| = std::abs(x), phase = arg(x) = std::arg(x) in radians [-pi, pi]. "
                            "Inverse of the MagPhasetoComplex block. "
                            "Signal chain: [complex DSP chain] -> ComplexToMagPhase -> [magnitude sink] + [phase sink].">;

    PortIn<std::complex<T>> in;
    PortOut<T>              mag;
    PortOut<T>              phase;

    GR_MAKE_REFLECTABLE(ComplexToMagPhase, in, mag, phase);

    [[nodiscard]] std::tuple<T, T> processOne(std::complex<T> x) const noexcept { return {std::abs(x), std::arg(x)}; }
};

} // namespace gr::incubator::basic
