#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <complex>

namespace gr::incubator::basic {

GR_REGISTER_BLOCK("gr::incubator::basic::MagPhasetoComplex", gr::incubator::basic::MagPhasetoComplex, ([T]), [ float, double ])

template<typename T>
struct MagPhasetoComplex : Block<MagPhasetoComplex<T>> {

    using Description = Doc<"Converts separate magnitude and phase streams to a complex output stream. "
                            "Each output sample is std::polar(mag, phase) = mag * exp(j*phase), "
                            "where phase is in radians. Inverse of the ComplexToMagPhase block. "
                            "Signal chain: [magnitude source] + [phase source] -> MagPhasetoComplex -> [complex DSP chain].">;

    PortIn<T>                mag;
    PortIn<T>                phase;
    PortOut<std::complex<T>> out;

    GR_MAKE_REFLECTABLE(MagPhasetoComplex, mag, phase, out);

    [[nodiscard]] constexpr std::complex<T> processOne(T m, T p) const noexcept { return std::polar(m, p); }
};

} // namespace gr::incubator::basic
