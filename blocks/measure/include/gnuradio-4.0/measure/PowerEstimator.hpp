#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <cmath>
#include <complex>

namespace gr::incubator::measure {
using namespace gr;

template<typename T>
struct PowerEstimator : Block<PowerEstimator<T>> {
    using Description = Doc<
        "IIR-smoothed signal power estimator. Tracks the running average of |x[n]|^2 "
        "with a single-pole IIR filter: P[n] = (1-alpha)*P[n-1] + alpha*|x[n]|^2. "
        "When output_db is true the output is 10*log10(P[n]); otherwise linear power. "
        "Signal chain (diagnostic / AGC monitor): AWGNChannel -> PowerEstimator -> (threshold check / plot).">;

    PortIn<std::complex<T>> in;
    PortOut<T>              out;

    Annotated<T, "alpha", Visible, Doc<"IIR smoothing coefficient (0 < alpha <= 1; smaller = slower, smoother)">> alpha = T(0.01);

    Annotated<bool, "output_db", Visible, Doc<"If true, output 10*log10(power) instead of linear power">> output_db = false;

    GR_MAKE_REFLECTABLE(PowerEstimator, in, out, alpha, output_db);

    T _power{T(0)};

    void start() { _power = T(0); }
    void settingsChanged(const property_map&, const property_map&) noexcept { _power = T(0); }

    [[nodiscard]] T processOne(std::complex<T> x) noexcept {
        _power = (T(1) - static_cast<T>(alpha)) * _power + static_cast<T>(alpha) * std::norm(x);
        if (static_cast<bool>(output_db)) {
            return T(10) * std::log10(std::max(_power, T(1e-12)));
        }
        return _power;
    }
};

GR_REGISTER_BLOCK("gr::incubator::measure::PowerEstimator", gr::incubator::measure::PowerEstimator, ([T]), [ float, double ])

} // namespace gr::incubator::measure
