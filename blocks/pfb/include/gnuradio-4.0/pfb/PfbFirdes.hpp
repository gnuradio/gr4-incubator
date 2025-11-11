/*
 * Copyright 2002,2007,2008,2012,2013,2018 Free Software Foundation, Inc.
 * Copyright 2026 GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>

#include <gnuradio-4.0/pfb/PfbWindow.hpp>

namespace gr::pfb::firdes {

inline void sanity_check_1f(double sampling_freq, double fa, double transition_width)
{
    if (sampling_freq <= 0.0)
        throw std::out_of_range("firdes check failed: sampling_freq > 0");
    if (fa <= 0.0 || fa > sampling_freq / 2)
        throw std::out_of_range("firdes check failed: 0 < fa <= sampling_freq / 2");
    if (transition_width <= 0)
        throw std::out_of_range("firdes check failed: transition_width > 0");
}

inline int compute_ntaps_windes(double sampling_freq,
                               double transition_width,
                               double attenuation_dB)
{
    int ntaps = static_cast<int>(attenuation_dB * sampling_freq / (22.0 * transition_width));
    if ((ntaps & 1) == 0) {
        ++ntaps;
    }
    return ntaps;
}

inline std::vector<float> low_pass_2(double gain,
                                    double sampling_freq,
                                    double cutoff_freq,
                                    double transition_width,
                                    double attenuation_dB,
                                    window::win_type window_type = window::win_type::WIN_BLACKMAN_HARRIS)
{
    sanity_check_1f(sampling_freq, cutoff_freq, transition_width);
    const int ntaps = compute_ntaps_windes(sampling_freq, transition_width, attenuation_dB);

    std::vector<float> taps(ntaps);
    std::vector<float> w = window::build(window_type, ntaps);

    const int m = (ntaps - 1) / 2;
    const double fwT0 = 2.0 * window::kPi * cutoff_freq / sampling_freq;
    for (int n = -m; n <= m; ++n) {
        if (n == 0) {
            taps[n + m] = static_cast<float>(fwT0 / window::kPi) * w[n + m];
        } else {
            taps[n + m] = static_cast<float>(std::sin(n * fwT0) / (n * window::kPi)) * w[n + m];
        }
    }

    double fmax = taps[m];
    for (int n = 1; n <= m; ++n) {
        fmax += 2.0 * taps[n + m];
    }

    const double scale = (fmax != 0.0) ? (gain / fmax) : 1.0;
    for (int i = 0; i < ntaps; ++i) {
        taps[i] = static_cast<float>(taps[i] * scale);
    }

    return taps;
}

} // namespace gr::pfb::firdes
