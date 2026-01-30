#pragma once

#include <cmath>
#include <utility>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/filter/time_domain_filter.hpp>

namespace gr::analog {

GR_REGISTER_BLOCK("FM Deemphasis Filter", gr::analog::FmDeemphasisFilter, ([T]), [ float, double ])

template<typename T>
requires std::floating_point<T>
struct FmDeemphasisFilter : Block<FmDeemphasisFilter<T>> {
    using Description = Doc<"FM deemphasis filter implemented as an IIR wrapper">;
    using Base = Block<FmDeemphasisFilter<T>>;
    using Base::Base;

    PortIn<T>  in;
    PortOut<T> out;

    float sample_rate = 400e3f;
    float tau = 75e-6f;
    GR_MAKE_REFLECTABLE(FmDeemphasisFilter, in, out, sample_rate, tau);

    [[nodiscard]] T processOne(T input) noexcept {
        return _iir.processOne(input);
    }

    void settingsChanged(const property_map& /*old_settings*/, const property_map& new_settings) {
        if (new_settings.contains("sample_rate") || new_settings.contains("tau")) {
            updateFilter();
        }
    }

    void start() {
        updateFilter();
    }

private:
    gr::filter::iir_filter<T, gr::filter::IIRForm::DF_II> _iir{};

    void updateFilter() {
        auto [b, a] = computeTaps();
        _iir.b = std::move(b);
        _iir.a = std::move(a);
        property_map new_settings{{"b", _iir.b}, {"a", _iir.a}};
        _iir.settingsChanged({}, new_settings);
    }

    [[nodiscard]] std::pair<std::vector<T>, std::vector<T>> computeTaps() const {
        const double sr = static_cast<double>(sample_rate);
        const double tau_s = static_cast<double>(tau);
        const double w_c = 1.0 / tau_s;
        const double w_ca = 2.0 * sr * std::tan(w_c / (2.0 * sr));
        const double k = -w_ca / (2.0 * sr);
        const double z1 = -1.0;
        const double p1 = (1.0 + k) / (1.0 - k);
        const double b0 = -k / (1.0 - k);

        std::vector<T> b{static_cast<T>(b0 * 1.0), static_cast<T>(b0 * -z1)};
        std::vector<T> a{static_cast<T>(1.0), static_cast<T>(-p1)};
        return {std::move(b), std::move(a)};
    }
};

} // namespace gr::analog
