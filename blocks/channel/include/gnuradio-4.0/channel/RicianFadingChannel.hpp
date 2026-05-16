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
struct RicianFadingChannel : Block<RicianFadingChannel<T>> {
    using Description = Doc<
        "Rician K-factor flat fading channel. Models a single-tap channel with a dominant "
        "line-of-sight (LOS) component plus scattered diffuse component. K = P_LOS/P_scatter. "
        "Channel model: h_scatter[n] = a*h_s[n-1] + aNoise*CN(0, P_scatter), "
        "h[n] = sqrt(K/(K+1))*exp(j*0) + h_scatter/sqrt(K+1), y[n] = h[n]*x[n] + w[n], "
        "where a = exp(-2*pi*max_doppler_norm) and E[|h|^2] = 1. "
        "K=0 gives pure Rayleigh (no LOS); K->inf gives near-constant gain (strong LOS, like AWGN). "
        "AWGN sigma^2 per dimension = 0.5/10^(snr_db/10).">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "k_factor", Visible, Doc<"Rician K-factor = P_LOS / P_scatter. 0 = pure Rayleigh, higher = stronger LOS.">> k_factor = T(1);

    Annotated<T, "snr_db", Visible, Doc<"Signal-to-noise ratio in dB">> snr_db = T(20);

    Annotated<T, "max_doppler_norm", Visible, Doc<"Normalised max Doppler f_D/f_s; 0.01 ≈ pedestrian speed">> max_doppler_norm = T(0.01);

    Annotated<uint64_t, "seed", Visible, Doc<"RNG seed">> seed = 42u;

    GR_MAKE_REFLECTABLE(RicianFadingChannel, in, out, k_factor, snr_db, max_doppler_norm, seed);

    std::complex<T>             _hScatter{T(0.5), T(0.5)};
    T                           _a{T(0)};
    T                           _aNoise{T(0)};
    T                           _noiseSigma{T(0)};
    std::mt19937_64             _rng;
    std::normal_distribution<T> _gauss{T(0), T(1)};

    void start() { _rebuild(); }
    void settingsChanged(const property_map&, const property_map&) noexcept { _rebuild(); }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> x) noexcept {
        // Update scatter component via AR(1)
        const std::complex<T> innov{_gauss(_rng) / std::sqrt(T(2)), _gauss(_rng) / std::sqrt(T(2))};
        _hScatter = _a * _hScatter + _aNoise * innov;

        // Total channel: LOS + scatter, normalised to unit average power
        const T               K      = static_cast<T>(k_factor);
        const T               kp1inv = T(1) / (K + T(1));
        const std::complex<T> hLOS{std::sqrt(K * kp1inv), T(0)};
        const std::complex<T> h = hLOS + _hScatter * std::sqrt(kp1inv);

        // AWGN
        const std::complex<T> noise{_gauss(_rng) * _noiseSigma, _gauss(_rng) * _noiseSigma};
        return h * x + noise;
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

GR_REGISTER_BLOCK("gr::incubator::channel::RicianFadingChannel", gr::incubator::channel::RicianFadingChannel, ([T]), [ float, double ])

} // namespace gr::incubator::channel
