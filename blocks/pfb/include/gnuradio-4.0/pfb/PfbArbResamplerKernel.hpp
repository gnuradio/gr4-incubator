/*
 * Copyright 2009,2010,2012 Free Software Foundation, Inc.
 * Copyright 2026 GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace gr::pfb::kernel {

template<typename T, typename TAPS_T = T>
class PfbArbResamplerKernel {
public:
    using sample_type = T;
    using taps_type = TAPS_T;

    PfbArbResamplerKernel() = default;

    PfbArbResamplerKernel(double rate, const std::vector<TAPS_T>& taps, std::size_t filter_size)
        : d_int_rate(static_cast<unsigned int>(filter_size)), d_rate(rate)
    {
        set_rate(rate);
        set_taps(taps);
    }

    void set_num_filters(std::size_t filter_size) {
        d_int_rate = static_cast<unsigned int>(std::max<std::size_t>(1, filter_size));
        set_rate(d_rate);
        if (!d_proto_taps.empty()) {
            set_taps(d_proto_taps);
        }
    }

    void set_taps(const std::vector<TAPS_T>& taps) {
        d_proto_taps = taps;
        if (d_int_rate == 0) {
            d_int_rate = 1;
        }

        d_last_filter = static_cast<unsigned int>((taps.size() / 2) % d_int_rate);

        std::vector<TAPS_T> dtaps;
        create_diff_taps(taps, dtaps);
        create_taps(taps, d_taps);
        create_taps(dtaps, d_dtaps);

        update_delay_and_phase();
    }

    void set_rate(double rate) {
        d_rate = (rate > 0.0) ? rate : 1.0;
        d_dec_rate = static_cast<unsigned int>(std::floor(static_cast<double>(d_int_rate) / d_rate));
        d_flt_rate = (static_cast<double>(d_int_rate) / d_rate) - static_cast<double>(d_dec_rate);
        update_delay_and_phase();
    }

    void set_phase(double ph) {
        if ((ph < 0.0) || (ph >= 2.0 * kPi)) {
            throw std::runtime_error("PfbArbResampler: set_phase value out of bounds [0, 2pi).");
        }
        const double ph_diff = 2.0 * kPi / static_cast<double>(d_int_rate);
        d_last_filter = static_cast<unsigned int>(ph / ph_diff);
    }

    double phase() const {
        const double ph_diff = 2.0 * kPi / static_cast<double>(d_int_rate);
        return static_cast<double>(d_last_filter) * ph_diff;
    }

    unsigned int taps_per_filter() const { return d_taps_per_filter; }
    unsigned int interpolation_rate() const { return d_int_rate; }
    unsigned int decimation_rate() const { return d_dec_rate; }
    double fractional_rate() const { return d_flt_rate; }
    int group_delay() const { return d_delay; }

    double phase_offset(double freq, double fs) const {
        const double adj = (2.0 * kPi) * (freq / fs) / static_cast<double>(d_int_rate);
        return -adj * d_est_phase_change;
    }

    template<typename InputAccessor>
    int filter(const InputAccessor& input,
               int n_to_read,
               sample_type* output,
               int output_capacity,
               int& n_read)
    {
        int i_out = 0;
        int i_in = 0;
        unsigned int j = d_last_filter;

        if (d_taps_per_filter == 0 || d_int_rate == 0) {
            n_read = 0;
            return 0;
        }

        while (i_in < n_to_read && i_out < output_capacity) {
            while (j < d_int_rate && i_in < n_to_read && i_out < output_capacity) {
                const std::size_t base = static_cast<std::size_t>(i_in) + d_taps_per_filter - 1;
                const sample_type o0 = dot(d_taps[j], input, base);
                const sample_type o1 = dot(d_dtaps[j], input, base);

                output[i_out] = o0 + scale_sample(o1, d_acc);
                ++i_out;

                d_acc += d_flt_rate;
                j += d_dec_rate + static_cast<unsigned int>(std::floor(d_acc));
                d_acc = std::fmod(d_acc, 1.0);
            }
            i_in += static_cast<int>(j / d_int_rate);
            j = j % d_int_rate;
        }

        d_last_filter = j;
        n_read = i_in;
        return i_out;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    std::vector<TAPS_T> d_proto_taps;
    std::vector<std::vector<TAPS_T>> d_taps;
    std::vector<std::vector<TAPS_T>> d_dtaps;

    unsigned int d_int_rate{32};
    unsigned int d_dec_rate{1};
    double d_flt_rate{0.0};
    double d_acc{0.0};
    unsigned int d_last_filter{0};
    unsigned int d_taps_per_filter{0};
    int d_delay{0};
    double d_est_phase_change{0.0};
    double d_rate{1.0};

    void create_diff_taps(const std::vector<TAPS_T>& newtaps, std::vector<TAPS_T>& difftaps) {
        difftaps.clear();
        if (newtaps.empty()) {
            return;
        }
        difftaps.reserve(newtaps.size());
        for (std::size_t i = 0; i + 1 < newtaps.size(); ++i) {
            difftaps.push_back(static_cast<TAPS_T>(-1) * newtaps[i] + newtaps[i + 1]);
        }
        difftaps.push_back(TAPS_T{});
    }

    void create_taps(const std::vector<TAPS_T>& newtaps,
                     std::vector<std::vector<TAPS_T>>& outtaps)
    {
        const std::size_t ntaps = newtaps.size();
        d_taps_per_filter = static_cast<unsigned int>(std::ceil(static_cast<double>(ntaps) / static_cast<double>(d_int_rate)));

        outtaps.clear();
        outtaps.resize(d_int_rate);

        std::vector<TAPS_T> tmp_taps = newtaps;
        tmp_taps.resize(static_cast<std::size_t>(d_int_rate) * d_taps_per_filter, TAPS_T{});

        for (unsigned int i = 0; i < d_int_rate; ++i) {
            outtaps[i].assign(d_taps_per_filter, TAPS_T{});
            for (unsigned int j = 0; j < d_taps_per_filter; ++j) {
                outtaps[i][j] = tmp_taps[i + j * d_int_rate];
            }
        }
    }

    void update_delay_and_phase() {
        if (d_taps_per_filter == 0) {
            d_delay = 0;
            d_est_phase_change = 0.0;
            return;
        }

        const double delay = d_rate * (static_cast<double>(d_taps_per_filter) - 1.0) / 2.0;
        d_delay = static_cast<int>(std::lround(delay));

        const double accum = static_cast<double>(d_delay) * d_flt_rate;
        const int accum_int = static_cast<int>(accum);
        const double accum_frac = accum - static_cast<double>(accum_int);
        const int end_filter = static_cast<int>(
            std::lround(std::fmod(static_cast<double>(d_last_filter) +
                                  static_cast<double>(d_delay) * static_cast<double>(d_dec_rate) +
                                  static_cast<double>(accum_int),
                                  static_cast<double>(d_int_rate))));

        d_est_phase_change = static_cast<double>(d_last_filter) - (static_cast<double>(end_filter) + accum_frac);
    }

    template<typename InputAccessor>
    sample_type dot(const std::vector<TAPS_T>& taps, const InputAccessor& input, std::size_t base) const {
        sample_type acc{};
        for (std::size_t i = 0; i < taps.size(); ++i) {
            acc += input[base - i] * taps[i];
        }
        return acc;
    }

    static sample_type scale_sample(const sample_type& value, double scale) {
        if constexpr (std::is_arithmetic_v<sample_type>) {
            return value * static_cast<sample_type>(scale);
        } else {
            using scalar_t = typename sample_type::value_type;
            return value * static_cast<scalar_t>(scale);
        }
    }
};

} // namespace gr::pfb::kernel
