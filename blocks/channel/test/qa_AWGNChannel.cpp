// qa_AWGNChannel.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <gnuradio-4.0/channel/AWGNChannel.hpp>

using namespace boost::ut;

#include <cmath>
#include <complex>
#include <format>
#include <numbers>

const boost::ut::suite<"AWGNChannel"> channelTests = [] {
    "output differs from input when noise is added"_test = [] {
        gr::incubator::channel::AWGNChannel<float> channel;
        channel.snr_db = 0.f; // high noise
        channel.seed   = 42ULL;
        channel.start();

        const std::complex<float> input{1.f, 0.f};
        bool                      anyDifference = false;
        for (int i = 0; i < 100; ++i) {
            if (channel.processOne(input) != input) {
                anyDifference = true;
                break;
            }
        }
        expect(anyDifference) << "AWGN channel produced no noise";
    };

    "high SNR preserves signal approximately"_test = [] {
        gr::incubator::channel::AWGNChannel<float> channel;
        channel.snr_db = 60.f; // very low noise
        channel.seed   = 1ULL;
        channel.start();

        float                     totalError = 0.f;
        const std::complex<float> input{1.f, 0.f};
        for (int i = 0; i < 1000; ++i) {
            const auto output = channel.processOne(input);
            totalError += std::abs(output - input);
        }
        expect(lt(totalError / 1000.f, 0.01f)) << "noise too high for 60dB SNR";
    };

    "phase offset rotates signal before noise"_test = [] {
        gr::incubator::channel::AWGNChannel<float> channel;
        channel.snr_db           = 60.f; // very low noise so rotation is visible
        channel.seed             = 1ULL;
        channel.phase_offset_rad = std::numbers::pi_v<float> / 2.f; // 90°
        channel.start();

        // Input is +1+0j; after 90° rotation it becomes +0+1j
        const std::complex<float> input{1.f, 0.f};
        float                     totalImag = 0.f;
        float                     totalReal = 0.f;
        for (int i = 0; i < 1000; ++i) {
            const auto output = channel.processOne(input);
            totalImag += output.imag();
            totalReal += std::abs(output.real());
        }
        expect(gt(totalImag / 1000.f, 0.99f)) << "90° rotation: Q should be ≈+1";
        expect(lt(totalReal / 1000.f, 0.01f)) << "90° rotation: I should be ≈0";
    };

    "zero phase offset leaves signal unrotated"_test = [] {
        gr::incubator::channel::AWGNChannel<float> channel;
        channel.snr_db           = 60.f;
        channel.seed             = 2ULL;
        channel.phase_offset_rad = 0.f;
        channel.start();

        const std::complex<float> input{1.f, 0.f};
        float                     totalReal = 0.f;
        for (int i = 0; i < 1000; ++i) {
            totalReal += channel.processOne(input).real();
        }
        expect(gt(totalReal / 1000.f, 0.99f)) << "zero offset: I should be ≈+1";
    };
};

const boost::ut::suite<"AWGNChannel extended"> awgnChannelExtTests = [] {
    using namespace boost::ut;

    "seed determinism: same seed → same output"_test = [] {
        gr::incubator::channel::AWGNChannel<float> ch1;
        ch1.snr_db           = 10.f;
        ch1.seed             = 42ULL;
        ch1.phase_offset_rad = 0.f;
        ch1.start();

        gr::incubator::channel::AWGNChannel<float> ch2;
        ch2.snr_db           = 10.f;
        ch2.seed             = 42ULL;
        ch2.phase_offset_rad = 0.f;
        ch2.start();

        const std::complex<float> in{1.f, 0.f};
        const auto                out1 = ch1.processOne(in);
        const auto                out2 = ch2.processOne(in);
        expect(approx(out1.real(), out2.real(), 1e-7f));
        expect(approx(out1.imag(), out2.imag(), 1e-7f));
    };

    "noise power matches σ²=1/(2·SNR_lin) formula"_test = [] {
        // At SNR_dB = 10 dB: SNR_lin = 10, σ² per component = 1/20
        // Total noise variance (I+Q) = 2 * σ² = 1/10
        constexpr float       kSnrDb  = 10.f;
        constexpr float       kSnrLin = 10.f;
        constexpr float       kSigma2 = 1.f / (2.f * kSnrLin); // per I/Q component
        constexpr std::size_t kN      = 100000;

        gr::incubator::channel::AWGNChannel<float> ch;
        ch.snr_db           = kSnrDb;
        ch.seed             = 12345ULL;
        ch.phase_offset_rad = 0.f;
        ch.start();

        // Feed DC=0 signal so output = noise only
        float sumI2 = 0.f, sumQ2 = 0.f;
        for (std::size_t i = 0; i < kN; ++i) {
            const auto n = ch.processOne({0.f, 0.f});
            sumI2 += n.real() * n.real();
            sumQ2 += n.imag() * n.imag();
        }
        const float varI = sumI2 / static_cast<float>(kN);
        const float varQ = sumQ2 / static_cast<float>(kN);
        // Allow 2% tolerance given Monte Carlo variance
        expect(approx(varI, kSigma2, 0.02f * kSigma2));
        expect(approx(varQ, kSigma2, 0.02f * kSigma2));
    };

    "phase_offset_rad rotates input signal"_test = [] {
        const float kPi = static_cast<float>(std::numbers::pi);

        gr::incubator::channel::AWGNChannel<float> ch;
        ch.snr_db           = 1000.f; // effectively noiseless
        ch.seed             = 1ULL;
        ch.phase_offset_rad = kPi / 2.f; // 90° rotation
        ch.start();

        // +1+0j rotated by 90° → 0+1j
        const auto out = ch.processOne({1.f, 0.f});
        expect(approx(out.real(), 0.f, 0.01f));
        expect(approx(out.imag(), 1.f, 0.01f));
    };

    "180° phase offset inverts BPSK constellation"_test = [] {
        const float kPi = static_cast<float>(std::numbers::pi);

        gr::incubator::channel::AWGNChannel<float> ch;
        ch.snr_db           = 1000.f; // noiseless
        ch.seed             = 2ULL;
        ch.phase_offset_rad = kPi; // 180° flip
        ch.start();

        const auto out1 = ch.processOne({1.f, 0.f});
        const auto out2 = ch.processOne({-1.f, 0.f});
        expect(approx(out1.real(), -1.f, 0.01f));
        expect(approx(out2.real(), 1.f, 0.01f));
    };
};

int main() {}
