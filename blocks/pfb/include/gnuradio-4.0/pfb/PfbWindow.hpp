/*
 * Copyright 2002,2007,2008,2012,2013 Free Software Foundation, Inc.
 * Copyright 2026 GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>

namespace gr::pfb::window {

constexpr float kPi = 3.14159265358979323846f;

enum class win_type {
    WIN_BLACKMAN_HARRIS = 5,
};

inline std::vector<float> coswindow(int ntaps, float c0, float c1, float c2)
{
    std::vector<float> taps(ntaps);
    const float m = static_cast<float>(ntaps - 1);
    for (int n = 0; n < ntaps; ++n) {
        taps[n] = c0 - c1 * std::cos((2.0f * kPi * n) / m) +
                  c2 * std::cos((4.0f * kPi * n) / m);
    }
    return taps;
}

inline std::vector<float> coswindow(int ntaps, float c0, float c1, float c2, float c3)
{
    std::vector<float> taps(ntaps);
    const float m = static_cast<float>(ntaps - 1);
    for (int n = 0; n < ntaps; ++n) {
        taps[n] = c0 - c1 * std::cos((2.0f * kPi * n) / m) +
                  c2 * std::cos((4.0f * kPi * n) / m) -
                  c3 * std::cos((6.0f * kPi * n) / m);
    }
    return taps;
}

inline std::vector<float> blackman_harris(int ntaps, int atten = 92)
{
    switch (atten) {
    case 61:
        return coswindow(ntaps, 0.42323f, 0.49755f, 0.07922f);
    case 67:
        return coswindow(ntaps, 0.44959f, 0.49364f, 0.05677f);
    case 74:
        return coswindow(ntaps, 0.40271f, 0.49703f, 0.09392f, 0.00183f);
    case 92:
        return coswindow(ntaps, 0.35875f, 0.48829f, 0.14128f, 0.01168f);
    default:
        throw std::out_of_range("window::blackman_harris: unknown attenuation value");
    }
}

inline std::vector<float> build(win_type type, int ntaps)
{
    switch (type) {
    case win_type::WIN_BLACKMAN_HARRIS:
        return blackman_harris(ntaps, 92);
    default:
        throw std::out_of_range("window::build: unsupported window type");
    }
}

} // namespace gr::pfb::window
