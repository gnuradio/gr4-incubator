#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <cmath>
#include <complex>
#include <numbers>

namespace gr::incubator::channel {
using namespace gr;

template<typename T>
struct IQImbalanceChannel : Block<IQImbalanceChannel<T>> {
    using Description = Doc<
        "I/Q amplitude and phase imbalance channel. Models front-end hardware imperfections where "
        "the I and Q paths have unequal gain and are not exactly 90 degrees apart. "
        "y[n] = (1+eps/2)*xi*exp(+j*phi/2) + j*(1-eps/2)*xq*exp(-j*phi/2), "
        "where eps is the amplitude imbalance (linear) and phi is the phase imbalance (radians). "
        "Creates an unwanted image at -f that degrades EVM, especially for high-order constellations (16-QAM+). "
        "The imbalance can be corrected with IQImbalanceCorrector (blind Gram-Schmidt). "
        "Signal chain: QAMModulator -> IQImbalanceChannel -> IQImbalanceCorrector -> QAMDemodulator.">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "amplitude_imbalance", Visible, Doc<"I/Q gain ratio deviation (linear; 0 = perfect, 0.1 = 10% gain error)">> amplitude_imbalance = T(0);

    Annotated<T, "phase_imbalance_deg", Visible, Doc<"I/Q phase error in degrees (0 = perfect, typical: 1–5 deg)">> phase_imbalance_deg = T(0);

    GR_MAKE_REFLECTABLE(IQImbalanceChannel, in, out, amplitude_imbalance, phase_imbalance_deg);

    [[nodiscard]] std::complex<T> processOne(std::complex<T> x) const noexcept {
        constexpr T deg2rad = static_cast<T>(std::numbers::pi) / T(180);
        const T     eps     = static_cast<T>(amplitude_imbalance);
        const T     phi     = static_cast<T>(phase_imbalance_deg) * deg2rad;

        const T xi = x.real();
        const T xq = x.imag();

        // y = (1+eps/2)*xi * exp(+j*phi/2) + j*(1-eps/2)*xq * exp(-j*phi/2)
        const T cp = std::cos(phi / T(2));
        const T sp = std::sin(phi / T(2));
        const T gp = T(1) + eps / T(2);
        const T gm = T(1) - eps / T(2);

        const T yr = gp * xi * cp - gm * xq * sp;
        const T yi = gp * xi * sp + gm * xq * cp;
        return {yr, yi};
    }
};

GR_REGISTER_BLOCK("gr::incubator::channel::IQImbalanceChannel", gr::incubator::channel::IQImbalanceChannel, ([T]), [ float, double ])

} // namespace gr::incubator::channel
