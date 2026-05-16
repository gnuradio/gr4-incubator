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
struct FlatFadingChannel : Block<FlatFadingChannel<T>> {
    using Description = Doc<
        "Single-tap flat (frequency-non-selective) Rayleigh or Rician fading channel with AWGN: "
        "y[n] = h[n]*x[n] + w[n], where h[n] follows an AR(1) process approximating the Clarke/Jakes "
        "Doppler spectrum: h[n] = alpha*h[n-1] + sqrt(1-alpha^2)*(gI+j*gQ)/sqrt(2), "
        "alpha = exp(-pi*max_doppler_norm). "
        "For Rician fading (k_factor > 0) a fixed LOS component is added: "
        "h[n] = h[n]/sqrt(K+1) + sqrt(K/(K+1))*exp(j*0), giving E[|h|^2] = 1. "
        "AWGN sigma^2 = 1/(2*10^(snr_db/10)) per complex dimension. "
        "Enables BER-vs-SNR curves showing fading degradation vs AWGNChannel. "
        "Pairs with LMSEqualizer for equalization demos.">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "snr_db", Visible, Doc<"Signal-to-noise ratio in dB (per-symbol Eb/N0 assuming unit symbol power)">> snr_db = T(20);

    Annotated<T, "max_doppler_norm", Visible, Doc<"Normalised max Doppler frequency (f_D / f_s); 0 = static (no fading)">> max_doppler_norm = T(0.01);

    Annotated<T, "k_factor", Visible, Doc<"Rician K factor: ratio of LOS to scattered power (0 = pure Rayleigh)">> k_factor = T(0);

    Annotated<uint64_t, "seed", Visible, Doc<"RNG seed for reproducibility">> seed = 42u;

    GR_MAKE_REFLECTABLE(FlatFadingChannel, in, out, snr_db, max_doppler_norm, k_factor, seed);

    std::complex<T>             _h{T(1), T(0)};
    std::mt19937_64             _rng;
    std::normal_distribution<T> _gauss{T(0), T(1)};
    T                           _alpha{T(0)};
    T                           _beta{T(0)}; // sqrt(1 - alpha²)
    T                           _noiseSigma{T(0)};

    void start() { _rebuild(); }
    void settingsChanged(const property_map&, const property_map&) noexcept { _rebuild(); }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> sample) noexcept {
        // Update fading tap via AR(1) process
        const std::complex<T> innov{_gauss(_rng) / std::sqrt(T(2)), _gauss(_rng) / std::sqrt(T(2))};
        _h = _alpha * _h + _beta * innov;

        // Add Rician LOS component if requested
        const T K = static_cast<T>(k_factor);
        if (K > T(0)) {
            const T kp1 = K + T(1);
            _h          = _h / std::sqrt(kp1) + std::complex<T>{std::sqrt(K / kp1), T(0)};
        }

        // AWGN
        const std::complex<T> noise{_gauss(_rng) * _noiseSigma, _gauss(_rng) * _noiseSigma};
        return _h * sample + noise;
    }

private:
    void _rebuild() noexcept {
        _rng.seed(static_cast<uint64_t>(seed));
        _gauss = std::normal_distribution<T>{T(0), T(1)};

        const T doppler = static_cast<T>(max_doppler_norm);
        _alpha          = (doppler > T(0)) ? std::exp(-static_cast<T>(std::numbers::pi) * doppler) : T(1) - T(1e-6); // near-static
        _beta           = std::sqrt(std::max(T(1) - _alpha * _alpha, T(0)));

        // AWGN: σ² per dimension = 1/(2·snr_linear)
        const T snrLin = std::pow(T(10), static_cast<T>(snr_db) / T(10));
        _noiseSigma    = std::sqrt(T(1) / (T(2) * snrLin));

        // Initialise channel tap: unit complex Gaussian / sqrt(2)
        _h = {_gauss(_rng) / std::sqrt(T(2)), _gauss(_rng) / std::sqrt(T(2))};
    }
};

GR_REGISTER_BLOCK("gr::incubator::channel::FlatFadingChannel", gr::incubator::channel::FlatFadingChannel, ([T]), [ float, double ])

} // namespace gr::incubator::channel
