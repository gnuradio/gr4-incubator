/*
 * Copyright 2004,2005,2009 Free Software Foundation, Inc.
 * Copyright 2026 GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <gnuradio-4.0/pfb/PfbRemez.hpp>

namespace gr::pfb::optfir {

inline double stopband_atten_to_dev(double atten_db) { return std::pow(10.0, -atten_db / 20.0); }

inline double passband_ripple_to_dev(double ripple_db)
{
    return (std::pow(10.0, ripple_db / 20.0) - 1.0) / (std::pow(10.0, ripple_db / 20.0) + 1.0);
}

inline double lporder(double freq1, double freq2, double delta_p, double delta_s)
{
    const double df = std::abs(freq2 - freq1);
    const double ddp = std::log10(delta_p);
    const double dds = std::log10(delta_s);

    const double a1 = 5.309e-3;
    const double a2 = 7.114e-2;
    const double a3 = -4.761e-1;
    const double a4 = -2.66e-3;
    const double a5 = -5.941e-1;
    const double a6 = -4.278e-1;

    const double b1 = 11.01217;
    const double b2 = 0.5124401;

    const double t1 = a1 * ddp * ddp;
    const double t2 = a2 * ddp;
    const double t3 = a4 * ddp * ddp;
    const double t4 = a5 * ddp;

    const double dinf = ((t1 + t2 + a3) * dds) + (t3 + t4 + a6);
    const double ff = b1 + b2 * (ddp - dds);
    const double n = dinf / df - ff * df + 1.0;
    return n;
}

inline std::tuple<int, std::vector<double>, std::vector<double>, std::vector<double>>
remezord(const std::vector<double>& fcuts,
         const std::vector<double>& mags,
         const std::vector<double>& devs,
         double fsamp = 2.0)
{
    auto f = fcuts;
    auto a = mags;
    auto d = devs;

    for (double& v : f) {
        v = v / fsamp;
    }

    const std::size_t nf = f.size();
    const std::size_t nm = a.size();
    const std::size_t nd = d.size();
    const std::size_t nbands = nm;

    if (nm != nd) {
        throw std::invalid_argument("remezord: length of mags and devs must be equal");
    }
    if (nf != 2 * (nbands - 1)) {
        throw std::invalid_argument("remezord: length of f must be 2 * len(mags) - 2");
    }

    for (std::size_t i = 0; i < nm; ++i) {
        if (a[i] != 0.0) {
            d[i] = d[i] / a[i];
        }
    }

    std::vector<double> f1;
    std::vector<double> f2;
    for (std::size_t i = 0; i < f.size(); i += 2) {
        f1.push_back(f[i]);
        f2.push_back(f[i + 1]);
    }

    std::size_t min_idx = 0;
    double min_delta = 2.0;
    for (std::size_t i = 0; i < f1.size(); ++i) {
        if (f2[i] - f1[i] < min_delta) {
            min_delta = f2[i] - f1[i];
            min_idx = i;
        }
    }

    double l = 0.0;
    if (nbands == 2) {
        l = lporder(f1[min_idx], f2[min_idx], d[0], d[1]);
    } else {
        for (std::size_t i = 1; i + 1 < nbands; ++i) {
            const double l1 = lporder(f1[i - 1], f2[i - 1], d[i], d[i - 1]);
            const double l2 = lporder(f1[i], f2[i], d[i], d[i + 1]);
            l = std::max(l, std::max(l1, l2));
        }
    }

    const int n = static_cast<int>(std::ceil(l)) - 1;

    std::vector<double> ff;
    ff.reserve(f.size() + 2);
    ff.push_back(0.0);
    for (double v : f) {
        ff.push_back(v * 2.0);
    }
    ff.push_back(1.0);

    std::vector<double> aa;
    for (double v : a) {
        aa.push_back(v);
        aa.push_back(v);
    }

    const double max_dev = *std::max_element(d.begin(), d.end());
    std::vector<double> wts(d.size(), 1.0);
    for (std::size_t i = 0; i < d.size(); ++i) {
        wts[i] = max_dev / d[i];
    }

    return {n, ff, aa, wts};
}

inline std::vector<double> low_pass(double gain,
                                    double fs,
                                    double freq1,
                                    double freq2,
                                    double passband_ripple_db,
                                    double stopband_atten_db,
                                    int nextra_taps = 2)
{
    if (freq2 <= freq1) {
        throw std::invalid_argument("low pass filter must have pass band below stop band");
    }

    const double passband_dev = passband_ripple_to_dev(passband_ripple_db);
    const double stopband_dev = stopband_atten_to_dev(stopband_atten_db);

    std::vector<double> desired_ampls{gain, 0.0};
    auto [n, fo, ao, w] = remezord({freq1, freq2}, desired_ampls, {passband_dev, stopband_dev}, fs);
    if (n == 0) {
        throw std::runtime_error("can't determine sufficient order for filter");
    }

    return pm_remez(n + nextra_taps, fo, ao, w, "bandpass", 16);
}

} // namespace gr::pfb::optfir
