#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>

namespace gr::incubator::channel {
using namespace gr;

template<typename T>
struct RayleighFadingChannel : Block<RayleighFadingChannel<T>> {
    using Description = Doc<
        "Flat Rayleigh fading channel. Models a single-tap channel with no line-of-sight component. "
        "The scattered field follows a 2D complex Gaussian AR(1) process: "
        "h[n] = a*h[n-1] + aNoise*CN(0,1), y[n] = h[n]*x[n] + w[n], "
        "where a = exp(-2*pi*max_doppler_norm), E[|h|^2] = 1 (normalised), "
        "and w[n] is AWGN with sigma^2 per dimension = 0.5/10^(snr_db/10). "
        "Equivalent to RicianFadingChannel with k_factor=0.">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "snr_db", Visible, Doc<"Signal-to-noise ratio in dB">> snr_db = T(20);

    Annotated<T, "max_doppler_norm", Visible, Doc<"Normalised max Doppler f_D/f_s; 0.01 ≈ pedestrian speed">> max_doppler_norm = T(0.01);

    Annotated<uint64_t, "seed", Visible, Doc<"RNG seed">> seed = 42u;

    GR_MAKE_REFLECTABLE(RayleighFadingChannel, in, out, snr_db, max_doppler_norm, seed);

    std::complex<T>             _hScatter{T(0.5), T(0.5)};
    T                           _a{T(0)};
    T                           _aNoise{T(0)};
    T                           _noiseSigma{T(0)};
    std::mt19937_64             _rng;
    std::normal_distribution<T> _gauss{T(0), T(1)};

    void start() { _rebuild(); }
    void settingsChanged(const property_map&, const property_map&) noexcept { _rebuild(); }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> x) noexcept {
        const std::complex<T> innov{_gauss(_rng) / std::sqrt(T(2)), _gauss(_rng) / std::sqrt(T(2))};
        _hScatter = _a * _hScatter + _aNoise * innov;

        const std::complex<T> noise{_gauss(_rng) * _noiseSigma, _gauss(_rng) * _noiseSigma};
        return _hScatter * x + noise;
    }

private:
    void _rebuild() noexcept {
        _rng.seed(static_cast<uint64_t>(seed));
        _gauss = std::normal_distribution<T>{T(0), T(1)};

        const T doppler = static_cast<T>(max_doppler_norm);
        _a              = (doppler > T(0)) ? std::exp(-T(2) * static_cast<T>(std::numbers::pi) * doppler) : T(1) - T(1e-6);
        _aNoise         = std::sqrt(std::max(T(1) - _a * _a, T(0)));

        const T snrLin = std::pow(T(10), static_cast<T>(snr_db) / T(10));
        _noiseSigma    = std::sqrt(T(0.5) / snrLin);

        _hScatter = {_gauss(_rng) / std::sqrt(T(2)), _gauss(_rng) / std::sqrt(T(2))};
    }
};

GR_REGISTER_BLOCK("gr::incubator::channel::RayleighFadingChannel", gr::incubator::channel::RayleighFadingChannel, ([T]), [ float, double ])

} // namespace gr::incubator::channel
