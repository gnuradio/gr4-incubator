#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace gr::incubator::measure {
using namespace gr;

template<typename T>
struct HistogramSink : Block<HistogramSink<T>> {
    using Description = Doc<
        "Amplitude/phase histogram sink. Accumulates a 1-D histogram of a scalar derived from "
        "each complex input sample. The scalar is extracted according to the mode setting: "
        "\"real\" (real part), \"imag\" (imaginary part), \"magnitude\" (|x|), \"phase\" (arg(x) in radians). "
        "Useful for: verifying constellation density (BPSK shows two peaks at +/-1), "
        "analysing noise distributions, measuring phase noise statistics. "
        "Accessors: counts() for histogram bin counts, bin_edges() for the left edge of each bin.">;

    PortIn<std::complex<T>> in;

    Annotated<uint32_t, "n_bins", Visible, Doc<"Number of histogram bins">>                         n_bins  = 64u;
    Annotated<T, "min_val", Visible, Doc<"Lower edge of histogram range">>                          min_val = T(-2);
    Annotated<T, "max_val", Visible, Doc<"Upper edge of histogram range">>                          max_val = T(2);
    Annotated<std::string, "mode", Visible, Doc<"\"real\", \"imag\", \"magnitude\", or \"phase\"">> mode    = std::string("real");

    GR_MAKE_REFLECTABLE(HistogramSink, in, n_bins, min_val, max_val, mode);

    std::vector<uint64_t> _counts;

    void start() { _rebuild(); }
    void settingsChanged(const property_map&, const property_map&) noexcept { _rebuild(); }

    void processOne(std::complex<T> x) noexcept {
        T                  v;
        const std::string& m = static_cast<std::string>(mode);
        if (m == "imag") {
            v = x.imag();
        } else if (m == "magnitude") {
            v = std::abs(x);
        } else if (m == "phase") {
            v = std::arg(x);
        } else {
            v = x.real(); // default: "real"
        }

        const T        lo = static_cast<T>(min_val);
        const T        hi = static_cast<T>(max_val);
        const uint32_t nb = static_cast<uint32_t>(n_bins);

        if (v < lo || v >= hi) {
            return;
        }

        const auto idx = static_cast<uint32_t>(static_cast<T>(nb) * (v - lo) / (hi - lo));
        if (idx < nb) {
            ++_counts[idx];
        }
    }

    [[nodiscard]] const std::vector<uint64_t>& counts() const noexcept { return _counts; }

    [[nodiscard]] std::vector<T> bin_edges() const {
        const uint32_t nb = static_cast<uint32_t>(n_bins);
        const T        lo = static_cast<T>(min_val);
        const T        hi = static_cast<T>(max_val);
        std::vector<T> edges(nb + 1u);
        for (uint32_t i = 0u; i <= nb; ++i) {
            edges[i] = lo + static_cast<T>(i) * (hi - lo) / static_cast<T>(nb);
        }
        return edges;
    }

    void reset() noexcept { std::fill(_counts.begin(), _counts.end(), uint64_t{0}); }

private:
    void _rebuild() { _counts.assign(static_cast<uint32_t>(n_bins), 0u); }
};

GR_REGISTER_BLOCK("gr::incubator::measure::HistogramSink", gr::incubator::measure::HistogramSink, ([T]), [ float, double ])

} // namespace gr::incubator::measure
