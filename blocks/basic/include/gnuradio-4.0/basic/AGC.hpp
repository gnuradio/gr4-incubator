#pragma once

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <complex>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::incubator::basic {
using namespace gr;

namespace agc_detail {
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
} // namespace agc_detail

template<typename T>
struct AGC : Block<AGC<T>> {
    using Scalar = typename agc_detail::sample_traits<T>::scalar_type;
    using Sample = typename agc_detail::sample_traits<T>::sample_type;

    using Description = Doc<"Automatic gain control (AGC). Applies a scalar gain to each input sample and updates "
                            "the gain each sample using a simple power-error feedback loop: "
                            "y = x * _gain, err = reference_power - |y|^2, _gain += rate * err, "
                            "_gain = clamp(_gain, min_gain, max_gain). "
                            "When |y|^2 is below reference_power the gain increases; when above it decreases. "
                            "rate is the loop bandwidth: larger values track faster but are noisier. "
                            "Typical use: insert between the receive front-end and the matched filter so "
                            "downstream blocks (Costas loop, timing sync, demodulator) always see near-unit-amplitude symbols.">;

    PortIn<Sample>  in;
    PortOut<Sample> out;

    Annotated<Scalar, "reference_power", Visible, Doc<"Target output power |y|² (linear)">>           reference_power = Scalar(1);
    Annotated<Scalar, "rate", Visible, Doc<"Loop update rate; larger = faster but noisier tracking">> rate            = Scalar(1e-3);
    Annotated<Scalar, "max_gain", Visible, Doc<"Upper gain clamp">>                                   max_gain        = Scalar(100);
    Annotated<Scalar, "min_gain", Visible, Doc<"Lower gain clamp">>                                   min_gain        = Scalar(1e-4);

    GR_MAKE_REFLECTABLE(AGC, in, out, reference_power, rate, max_gain, min_gain);

    Scalar _gain{Scalar(1)};

    void start() { _gain = Scalar(1); }

    void settingsChanged(const property_map& /*old*/, const property_map& /*new*/) { _gain = Scalar(1); }

    [[nodiscard]] Sample processOne(Sample sample) noexcept {
        const Sample y     = sample * _gain;
        const Scalar power = y.real() * y.real() + y.imag() * y.imag();
        _gain += rate * (reference_power - power);
        if (_gain < static_cast<Scalar>(min_gain)) {
            _gain = static_cast<Scalar>(min_gain);
        }
        if (_gain > static_cast<Scalar>(max_gain)) {
            _gain = static_cast<Scalar>(max_gain);
        }
        return y;
    }
};

GR_REGISTER_BLOCK("gr::incubator::basic::AGC", gr::incubator::basic::AGC, ([T]), [ uint8_t, int16_t, int32_t, float, double, std::complex<float>, std::complex<double> ])

} // namespace gr::incubator::basic
