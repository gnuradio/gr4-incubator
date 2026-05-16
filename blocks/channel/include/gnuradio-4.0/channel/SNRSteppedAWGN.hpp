#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <random>

namespace gr::incubator::channel {
using namespace gr;

template<typename T>
struct SNRSteppedAWGN : Block<SNRSteppedAWGN<T>, NoDefaultTagForwarding> {
    using Description = Doc<
        "AWGN channel that sweeps SNR from snr_start_db to snr_stop_db in snr_step_db increments, "
        "intended for BER-vs-SNR curve measurement in a single graph run. "
        "Emits a property_map{\"snr_db\": float} tag at the first output sample of each step. "
        "Number of steps = floor((snr_stop_db - snr_start_db) / snr_step_db) + 1; "
        "each step is exactly samples_per_step output samples. "
        "Calls requestStop() after the last step completes. "
        "sigma per I/Q = 1/sqrt(2*SNR_linear) (unit signal power convention, same as AWGNChannel).">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<float, "snr_start_db", Visible, Doc<"First SNR value in dB.">>                                   snr_start_db     = 0.f;
    Annotated<float, "snr_stop_db", Visible, Doc<"Last SNR value in dB.">>                                     snr_stop_db      = 20.f;
    Annotated<float, "snr_step_db", Visible, Doc<"SNR increment per step.">>                                   snr_step_db      = 2.f;
    Annotated<gr::Size_t, "samples_per_step", Visible, Doc<"Number of samples to process at each SNR level.">> samples_per_step = gr::Size_t{10000};
    Annotated<std::uint64_t, "seed", Visible, Doc<"RNG seed (0 = non-deterministic).">>                        seed             = 42ULL;

    GR_MAKE_REFLECTABLE(SNRSteppedAWGN, in, out, snr_start_db, snr_stop_db, snr_step_db, samples_per_step, seed);

    uint32_t    _snrIndex{0u};
    uint32_t    _numSteps{0u};
    std::size_t _samplesRemainingInStep{0u};
    float       _currentSnr{0.f};
    T           _sigma{T(1)};
    bool        _done{false};

    std::mt19937_64             _rng;
    std::normal_distribution<T> _dist{T(0), T(1)};

    void start() { _init(); }
    void settingsChanged(const property_map& /*old*/, const property_map& /*new*/) { _init(); }

    [[nodiscard]] work::Status processBulk(InputSpanLike auto& inSpan, OutputSpanLike auto& outSpan) noexcept {
        for (std::size_t i = 0u; i < inSpan.size(); ++i) {
            if (_samplesRemainingInStep == 0u && !_done) {
                gr::property_map tagMap;
                tagMap["snr_db"] = _currentSnr;
                outSpan.publishTag(tagMap, i);
                const T snrLin          = std::pow(T(10), static_cast<T>(_currentSnr) / T(10));
                _sigma                  = T(1) / std::sqrt(T(2) * snrLin);
                _samplesRemainingInStep = static_cast<std::size_t>(samples_per_step.value);
            }

            outSpan[i] = inSpan[i] + std::complex<T>{_dist(_rng) * _sigma, _dist(_rng) * _sigma};

            if (!_done) {
                --_samplesRemainingInStep;
                if (_samplesRemainingInStep == 0u) {
                    ++_snrIndex;
                    if (_snrIndex < _numSteps) {
                        _currentSnr += snr_step_db.value;
                    } else {
                        _done = true;
                        this->requestStop();
                    }
                }
            }
        }
        return work::Status::OK;
    }

private:
    void _init() noexcept {
        if (seed == 0ULL) {
            _rng.seed(std::random_device{}());
        } else {
            _rng.seed(static_cast<uint64_t>(seed));
        }
        _snrIndex               = 0u;
        _currentSnr             = snr_start_db.value;
        _done                   = false;
        const float range       = snr_stop_db.value - snr_start_db.value;
        const float step        = snr_step_db.value;
        _numSteps               = (step > 0.f && range >= 0.f) ? static_cast<uint32_t>(std::floor(range / step)) + 1u : 1u;
        _samplesRemainingInStep = 0u; // triggers initial tag on first call
    }
};

GR_REGISTER_BLOCK("gr::incubator::channel::SNRSteppedAWGN", gr::incubator::channel::SNRSteppedAWGN, ([T]), [ float, double ])

} // namespace gr::incubator::channel
