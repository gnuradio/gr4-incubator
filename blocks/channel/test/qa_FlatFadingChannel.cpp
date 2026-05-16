// qa_FlatFadingChannel.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <gnuradio-4.0/channel/AWGNChannel.hpp>
#include <gnuradio-4.0/channel/FlatFadingChannel.hpp>
#include <numbers>
#include <vector>

using namespace boost::ut;

const boost::ut::suite<"FlatFadingChannel"> flatFadingTests = [] {
    using namespace boost::ut;

    "output power ≈ input power at high SNR"_test = [] {
        gr::incubator::channel::FlatFadingChannel<float> ch;
        ch.snr_db           = 40.f;
        ch.max_doppler_norm = 0.01f;
        ch.start();

        float         sumPow = 0.f;
        constexpr int N      = 10000;
        for (int i = 0; i < N; ++i) {
            const auto y = ch.processOne({1.f, 0.f});
            sumPow += std::norm(y);
        }
        const float avgPow = sumPow / N;
        // Mean |h|² ≈ 1 for Rayleigh; with high SNR noise is negligible
        expect(gt(avgPow, 0.5f) && lt(avgPow, 2.f)) << std::format("avg power={:.3f}", avgPow);
    };

    "static channel barely changes"_test = [] {
        gr::incubator::channel::FlatFadingChannel<float> ch;
        ch.snr_db           = 60.f;
        ch.max_doppler_norm = 0.0f; // static
        ch.start();

        // With doppler=0 alpha≈1, channel tap changes very slowly
        std::complex<float> first = ch.processOne({1.f, 0.f});
        std::complex<float> last  = first;
        for (int i = 1; i < 100; ++i) {
            last = ch.processOne({1.f, 0.f});
        }

        // Phase may drift; check magnitude of change is small
        const float change = std::abs(last - first) / std::max(std::abs(first), 1e-6f);
        expect(lt(change, 0.5f)) << std::format("tap change={:.4f}", change);
    };

    "Rician K>0 raises mean amplitude"_test = [] {
        auto meanAmp = [](float K) {
            gr::incubator::channel::FlatFadingChannel<float> ch;
            ch.snr_db           = 60.f;
            ch.max_doppler_norm = 0.01f;
            ch.k_factor         = K;
            ch.start();
            float sum = 0.f;
            for (int i = 0; i < 5000; ++i) {
                sum += std::abs(ch.processOne({1.f, 0.f}));
            }
            return sum / 5000.f;
        };
        expect(gt(meanAmp(10.f), meanAmp(0.f))) << "Rician K>0 should raise mean";
    };
};

const boost::ut::suite<"FlatFadingChannel extended"> flatFadingExtTests = [] {
    "start resets RNG state so two calls produce identical sequences"_test = [] {
        gr::incubator::channel::FlatFadingChannel<float> ch;
        ch.snr_db           = 40.f;
        ch.max_doppler_norm = 0.01f;
        ch.seed             = 7u;
        ch.start();

        const std::complex<float>        in{1.f, 0.f};
        std::vector<std::complex<float>> first(20), second(20);
        for (auto& s : first) {
            s = ch.processOne(in);
        }

        ch.start(); // reset
        for (auto& s : second) {
            s = ch.processOne(in);
        }

        bool allEqual = true;
        for (std::size_t i = 0; i < first.size(); ++i) {
            if (first[i] != second[i]) {
                allEqual = false;
                break;
            }
        }
        expect(allEqual) << "start() must reset state to produce reproducible output";
    };

    "average output power is close to input power at high SNR"_test = [] {
        gr::incubator::channel::FlatFadingChannel<float> ch;
        ch.snr_db           = 60.f; // negligible AWGN
        ch.max_doppler_norm = 0.01f;
        ch.seed             = 42u;
        ch.start();

        const std::complex<float> in{1.f, 0.f};
        double                    sumPower = 0.0;
        constexpr int             N        = 5000;
        for (int i = 0; i < N; ++i) {
            const auto y = ch.processOne(in);
            sumPower += static_cast<double>(std::norm(y));
        }
        const double meanPower = sumPower / N;
        // Fading has unit mean channel power; at high SNR mean output ≈ 1
        expect(meanPower > 0.2) << std::format("mean power={:.4f} too low", meanPower);
        expect(meanPower < 5.0) << std::format("mean power={:.4f} too high", meanPower);
    };

    "k_factor large causes near-constant amplitude (Rician K>>1)"_test = [] {
        gr::incubator::channel::FlatFadingChannel<float> ch;
        ch.snr_db           = 60.f;
        ch.max_doppler_norm = 0.001f;
        ch.k_factor         = 100.f; // strong LOS; little variation
        ch.seed             = 1u;
        ch.start();

        const std::complex<float> in{1.f, 0.f};
        std::vector<float>        amps;
        amps.reserve(500);
        for (int i = 0; i < 500; ++i) {
            amps.push_back(std::abs(ch.processOne(in)));
        }
        const float minAmp = *std::min_element(amps.begin(), amps.end());
        const float maxAmp = *std::max_element(amps.begin(), amps.end());
        // With K=100 the spread should be much tighter than Rayleigh
        expect(maxAmp - minAmp < 0.5f) << std::format("Rician K=100 amplitude variation too wide: [{:.3f},{:.3f}]", minAmp, maxAmp);
    };
};

int main() {}
