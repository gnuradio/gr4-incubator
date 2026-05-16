#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <cmath>
#include <complex>
#include <random>

namespace gr::incubator::channel {
using namespace gr;

template<typename T>
struct AWGNChannel : Block<AWGNChannel<T>> {
    using Description = Doc<
        "Additive White Gaussian Noise (AWGN) channel. Models the simplest baseband impairment: "
        "complex Gaussian noise added to every input sample: y[n] = x[n]*exp(j*phase_offset_rad) + w[n], "
        "where w_I, w_Q ~ N(0, sigma^2) with sigma^2 = 1/(2*10^(snr_db/10)). "
        "Assumes unit signal power (|x[n]|^2 ~= 1); scale snr_db for other signal powers. "
        "phase_offset_rad simulates a fixed carrier-phase or LO mismatch applied before noise. "
        "Use seed=0 for non-deterministic noise; any non-zero seed gives a fully reproducible run. "
        "Typical use: BER-vs-SNR baseline (theory: BER=Q(sqrt(2*SNR)) for BPSK), "
        "sensitivity testing of downstream DSP blocks, or channel simulation in loopback test graphs. "
        "Signal chain: [Modulator] -> AWGNChannel -> [Demodulator / BERSink].">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "snr_db", Visible, Doc<"signal-to-noise ratio in dB (assumes unit signal power)">>        snr_db           = T(10);
    Annotated<std::uint64_t, "seed", Visible, Doc<"RNG seed (0: non-deterministic)">>                      seed             = 0ULL;
    Annotated<T, "phase_offset_rad", Visible, Doc<"carrier phase offset in radians applied before noise">> phase_offset_rad = T(0);

    GR_MAKE_REFLECTABLE(AWGNChannel, in, out, snr_db, seed, phase_offset_rad);

    std::mt19937_64             _rng;
    std::normal_distribution<T> _dist{T(0), T(1)};

    void start() { _seedRng(); }

    void settingsChanged(const property_map& /*old*/, const property_map& /*new*/) { _seedRng(); }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> sample) noexcept {
        // Apply carrier phase offset (simulates LO mismatch / channel rotation)
        const std::complex<T> rotated = sample * std::polar(T(1), T(phase_offset_rad));
        // noise std per I/Q component: sigma = 1 / sqrt(2 * SNR_linear)
        const T snrLinear = std::pow(T(10), snr_db / T(10));
        const T sigma     = T(1) / std::sqrt(T(2) * snrLinear);
        return rotated + std::complex<T>{_dist(_rng) * sigma, _dist(_rng) * sigma};
    }

private:
    void _seedRng() {
        if (seed == 0ULL) {
            _rng.seed(std::random_device{}());
        } else {
            _rng.seed(seed);
        }
    }
};

GR_REGISTER_BLOCK("gr::incubator::channel::AWGNChannel", gr::incubator::channel::AWGNChannel, ([T]), [ float, double ])

} // namespace gr::incubator::channel
