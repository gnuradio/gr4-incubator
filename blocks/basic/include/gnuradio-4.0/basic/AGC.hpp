#pragma once

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <complex>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::incubator::basic {
using namespace gr;

template<typename T>
struct AGC : Block<AGC<T>> {
    using Description = Doc<"Automatic gain control (AGC). Applies a scalar gain to each input sample and updates "
                            "the gain each sample using a simple power-error feedback loop: "
                            "y = x * _gain, err = reference_power - |y|^2, _gain += rate * err, "
                            "_gain = clamp(_gain, min_gain, max_gain). "
                            "When |y|^2 is below reference_power the gain increases; when above it decreases. "
                            "rate is the loop bandwidth: larger values track faster but are noisier. "
                            "Typical use: insert between the receive front-end and the matched filter so "
                            "downstream blocks (Costas loop, timing sync, demodulator) always see near-unit-amplitude symbols.">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "reference_power", Visible, Doc<"Target output power |y|² (linear)">>           reference_power = T(1);
    Annotated<T, "rate", Visible, Doc<"Loop update rate; larger = faster but noisier tracking">> rate            = T(1e-3);
    Annotated<T, "max_gain", Visible, Doc<"Upper gain clamp">>                                   max_gain        = T(100);
    Annotated<T, "min_gain", Visible, Doc<"Lower gain clamp">>                                   min_gain        = T(1e-4);

    GR_MAKE_REFLECTABLE(AGC, in, out, reference_power, rate, max_gain, min_gain);

    T _gain{T(1)};

    void start() { _gain = T(1); }

    void settingsChanged(const property_map& /*old*/, const property_map& /*new*/) { _gain = T(1); }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> sample) noexcept {
        const std::complex<T> y     = sample * _gain;
        const T               power = y.real() * y.real() + y.imag() * y.imag();
        _gain += rate * (reference_power - power);
        if (_gain < static_cast<T>(min_gain)) {
            _gain = static_cast<T>(min_gain);
        }
        if (_gain > static_cast<T>(max_gain)) {
            _gain = static_cast<T>(max_gain);
        }
        return y;
    }
};

GR_REGISTER_BLOCK("gr::incubator::basic::AGC", gr::incubator::basic::AGC, ([T]), [ uint8_t, int16_t, int32_t, float, double, std::complex<float>, std::complex<double> ])

} // namespace gr::incubator::basic
