/*
 * Copyright 2009,2010,2012 Free Software Foundation, Inc.
 * Copyright 2026 GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <complex>
#include <cstddef>
#include <type_traits>
#include <vector>

#include <gnuradio-4.0/pfb/PfbFirdes.hpp>
#include <gnuradio-4.0/pfb/PfbOptfir.hpp>

namespace gr::pfb {

template <typename T>
struct is_complex : std::false_type {};

template <typename T>
struct is_complex<std::complex<T>> : std::true_type {};

// C++-only taps generator modeled after GR3 pfb.py create_taps logic.
// Returns taps at the interpolated sample rate (num_filters).
template <typename TAPS_T>
std::vector<TAPS_T> create_taps(double rate, std::size_t num_filters, double attenuation_db)
{
    const double percent = 0.80;
    std::vector<TAPS_T> taps;

    if (rate < 1.0) {
        const double halfband = 0.5 * rate;
        const double bw = percent * halfband;
        const double tb = (percent / 2.0) * halfband;

        auto real_taps = firdes::low_pass_2(static_cast<double>(num_filters),
                                            static_cast<double>(num_filters),
                                            bw,
                                            tb,
                                            attenuation_db,
                                            window::win_type::WIN_BLACKMAN_HARRIS);
        taps.reserve(real_taps.size());
        if constexpr (is_complex<TAPS_T>::value) {
            for (float t : real_taps) {
                taps.emplace_back(static_cast<typename TAPS_T::value_type>(t),
                                  static_cast<typename TAPS_T::value_type>(0));
            }
        } else {
            for (float t : real_taps) {
                taps.emplace_back(static_cast<TAPS_T>(t));
            }
        }
        return taps;
    }

    const double halfband = 0.5;
    const double bw = percent * halfband;
    const double tb = (percent / 2.0) * halfband;

    double ripple = 0.1;
    while (true) {
        try {
            const auto real_taps = optfir::low_pass(static_cast<double>(num_filters),
                                                   static_cast<double>(num_filters),
                                                   bw,
                                                   bw + tb,
                                                   ripple,
                                                   attenuation_db);
            taps.reserve(real_taps.size());
            if constexpr (is_complex<TAPS_T>::value) {
                for (double t : real_taps) {
                    taps.emplace_back(static_cast<typename TAPS_T::value_type>(t),
                                      static_cast<typename TAPS_T::value_type>(0));
                }
            } else {
                for (double t : real_taps) {
                    taps.emplace_back(static_cast<TAPS_T>(t));
                }
            }
            return taps;
        } catch (const std::runtime_error&) {
            ripple += 0.01;
            if (ripple >= 1.0) {
                throw;
            }
        }
    }
}

} // namespace gr::pfb
