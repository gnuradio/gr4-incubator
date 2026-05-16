#pragma once

#include <gnuradio-4.0/Block.hpp>

#include <complex>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::incubator::basic {
using namespace gr;

template<typename T>
struct DCBlocker : Block<DCBlocker<T>> {
    using Description = Doc<
        "First-order IIR DC-blocking filter. Removes the DC component while passing all AC signals "
        "using the high-pass difference equation: y[n] = x[n] - x[n-1] + alpha*y[n-1]. "
        "The -3 dB cutoff is approximately f_c ~= (1-alpha)/(2*pi)*f_s; "
        "for alpha=0.999, f_c ~= 0.000159*f_s (e.g. 159 Hz at 1 Msps). "
        "Operates on std::complex<T> by applying the filter independently to real and imaginary parts. "
        "Signal chain (direct-conversion receiver): ADC -> DCBlocker -> IQImbalanceCorrector -> AGC -> CostasLoop.">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "alpha", Visible, Doc<"Pole radius; closer to 1 = narrower notch, slower settling (typical: 0.99 to 0.9999)">> alpha = T(0.999);

    GR_MAKE_REFLECTABLE(DCBlocker, in, out, alpha);

    std::complex<T> _x_prev{T(0), T(0)};
    std::complex<T> _y_prev{T(0), T(0)};

    void start() {
        _x_prev = {};
        _y_prev = {};
    }
    void settingsChanged(const property_map&, const property_map&) noexcept {
        _x_prev = {};
        _y_prev = {};
    }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> x) noexcept {
        const T               a = static_cast<T>(alpha);
        const std::complex<T> y = x - _x_prev + a * _y_prev;
        _x_prev                 = x;
        _y_prev                 = y;
        return y;
    }
};

GR_REGISTER_BLOCK("gr::incubator::basic::DCBlocker", gr::incubator::basic::DCBlocker, ([T]), [ uint8_t, int16_t, int32_t, float, double, std::complex<float>, std::complex<double> ])

} // namespace gr::incubator::basic
