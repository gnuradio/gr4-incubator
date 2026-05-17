#pragma once

#include <gnuradio-4.0/Block.hpp>

#include <complex>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::incubator::basic {
using namespace gr;

namespace dc_blocker_detail {
template<typename T>
struct sample_traits {
    using scalar_type = T;
    using sample_type = std::complex<T>;
};

template<typename T>
struct sample_traits<std::complex<T>> {
    using scalar_type = T;
    using sample_type = std::complex<T>;
};
} // namespace dc_blocker_detail

template<typename T>
struct DCBlocker : Block<DCBlocker<T>> {
    using Scalar = typename dc_blocker_detail::sample_traits<T>::scalar_type;
    using Sample = typename dc_blocker_detail::sample_traits<T>::sample_type;

    using Description = Doc<"First-order IIR DC-blocking filter. Removes the DC component while passing all AC signals "
                            "using the high-pass difference equation: y[n] = x[n] - x[n-1] + alpha*y[n-1]. "
                            "The -3 dB cutoff is approximately f_c ~= (1-alpha)/(2*pi)*f_s; "
                            "for alpha=0.999, f_c ~= 0.000159*f_s (e.g. 159 Hz at 1 Msps). "
                            "Operates on std::complex<T> by applying the filter independently to real and imaginary parts. "
                            "Signal chain (direct-conversion receiver): ADC -> DCBlocker -> IQImbalanceCorrector -> AGC -> CostasLoop.">;

    PortIn<Sample>  in;
    PortOut<Sample> out;

    Annotated<Scalar, "alpha", Visible, Doc<"Pole radius; closer to 1 = narrower notch, slower settling (typical: 0.99 to 0.9999)">> alpha = Scalar(0.999);

    GR_MAKE_REFLECTABLE(DCBlocker, in, out, alpha);

    Sample _x_prev{};
    Sample _y_prev{};

    void start() {
        _x_prev = {};
        _y_prev = {};
    }
    void settingsChanged(const property_map&, const property_map&) noexcept {
        _x_prev = {};
        _y_prev = {};
    }

    [[nodiscard]] Sample processOne(Sample x) noexcept {
        const Scalar a = static_cast<Scalar>(alpha);
        const Sample y = x - _x_prev + a * _y_prev;
        _x_prev        = x;
        _y_prev        = y;
        return y;
    }
};

GR_REGISTER_BLOCK("gr::incubator::basic::DCBlocker", gr::incubator::basic::DCBlocker, ([T]), [ uint8_t, int16_t, int32_t, float, double, std::complex<float>, std::complex<double> ])

} // namespace gr::incubator::basic
