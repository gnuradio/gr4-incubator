/*
 * Copyright 2012,2013 Free Software Foundation, Inc.
 * Copyright 2026 GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <boost/ut.hpp>
#include <complex>
#include <vector>
#include <cmath>

#include <gnuradio-4.0/pfb/PfbArbResamplerKernel.hpp>
#include <gnuradio-4.0/pfb/PfbArbResamplerTaps.hpp>

using namespace boost::ut;
using gr::pfb::kernel::PfbArbResamplerKernel;

namespace {

constexpr double kPi = 3.14159265358979323846;

static std::vector<std::complex<float>> sig_source_c(double samp_rate, double freq, std::size_t n) {
    std::vector<std::complex<float>> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / samp_rate;
        out.emplace_back(static_cast<float>(std::cos(2.0 * kPi * freq * t)),
                         static_cast<float>(std::sin(2.0 * kPi * freq * t)));
    }
    return out;
}

} // namespace

const suite PfbArbResamplerTests = [] {

    "ccf_rate_gt_1"_test = [] {
        const std::size_t n = 2000;
        const double fs = 5000.0;
        const double rrate = 2.4321;
        const std::size_t nfilts = 32;
        const double atten = 80.0;

        auto taps = gr::pfb::create_taps<float>(rrate, nfilts, atten);
        PfbArbResamplerKernel<std::complex<float>, float> kernel(rrate, taps, nfilts);

        auto data = sig_source_c(fs, 211.123, n);
        const std::size_t k = kernel.taps_per_filter();

        std::vector<std::complex<float>> input(k > 0 ? k - 1 : 0, std::complex<float>{});
        input.insert(input.end(), data.begin(), data.end());

        const std::size_t max_out = static_cast<std::size_t>(std::ceil(n * rrate)) + 64;
        std::vector<std::complex<float>> output(max_out, std::complex<float>{});

        int n_read = 0;
        const int n_produced = kernel.filter(input, static_cast<int>(n), output.data(), static_cast<int>(max_out), n_read);
        expect(n_read > 0_i);
        expect(n_produced > 0_i);

        const int delay = kernel.group_delay();
        const double phase = kernel.phase_offset(211.123, fs);

        const std::size_t ntest = 20;
        for (std::size_t i = 0; i < ntest; ++i) {
            const std::size_t idx = static_cast<std::size_t>(n_produced - ntest + i);
            const double t = (static_cast<double>(static_cast<int>(idx) - delay)) / (fs * rrate);
            const std::complex<float> expected{
                static_cast<float>(std::cos(2.0 * kPi * 211.123 * t + phase)),
                static_cast<float>(std::sin(2.0 * kPi * 211.123 * t + phase))
            };
            const auto err = std::abs(output[idx] - expected);
            expect(err < 0.05f);
        }
    };

    "ccf_rate_lt_1"_test = [] {
        const std::size_t n = 5000;
        const double fs = 5000.0;
        const double rrate = 0.75;
        const std::size_t nfilts = 32;
        const double atten = 80.0;

        auto taps = gr::pfb::create_taps<float>(rrate, nfilts, atten);
        PfbArbResamplerKernel<std::complex<float>, float> kernel(rrate, taps, nfilts);

        auto data = sig_source_c(fs, 211.123, n);
        const std::size_t k = kernel.taps_per_filter();

        std::vector<std::complex<float>> input(k > 0 ? k - 1 : 0, std::complex<float>{});
        input.insert(input.end(), data.begin(), data.end());

        const std::size_t max_out = static_cast<std::size_t>(std::ceil(n * rrate)) + 64;
        std::vector<std::complex<float>> output(max_out, std::complex<float>{});

        int n_read = 0;
        const int n_produced = kernel.filter(input, static_cast<int>(n), output.data(), static_cast<int>(max_out), n_read);
        expect(n_read > 0_i);
        expect(n_produced > 0_i);

        const int delay = kernel.group_delay();
        const double phase = kernel.phase_offset(211.123, fs);

        const std::size_t ntest = 20;
        for (std::size_t i = 0; i < ntest; ++i) {
            const std::size_t idx = static_cast<std::size_t>(n_produced - ntest + i);
            const double t = (static_cast<double>(static_cast<int>(idx) - delay)) / (fs * rrate);
            const std::complex<float> expected{
                static_cast<float>(std::cos(2.0 * kPi * 211.123 * t + phase)),
                static_cast<float>(std::sin(2.0 * kPi * 211.123 * t + phase))
            };
            const auto err = std::abs(output[idx] - expected);
            expect(err < 0.05f);
        }
    };
};

int main() {
    return boost::ut::cfg<boost::ut::override>.run();
}
