// qa_IQImbalanceChannel.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <gnuradio-4.0/channel/IQImbalanceChannel.hpp>
#include <random>
#include <vector>

using namespace boost::ut;

const boost::ut::suite<"IQImbalanceChannel"> iqImbalTests = [] {
    using namespace boost::ut;

    "zero imbalance is identity"_test = [] {
        gr::incubator::channel::IQImbalanceChannel<float> ch;
        ch.amplitude_imbalance = 0.f;
        ch.phase_imbalance_deg = 0.f;
        const std::complex<float> s{0.8f, -0.6f};
        const auto                y = ch.processOne(s);
        expect(approx(y.real(), s.real(), 1e-5f));
        expect(approx(y.imag(), s.imag(), 1e-5f));
    };

    "amplitude imbalance scales I vs Q"_test = [] {
        gr::incubator::channel::IQImbalanceChannel<float> ch;
        ch.amplitude_imbalance = 0.2f; // 20% gain error
        ch.phase_imbalance_deg = 0.f;
        // Pure real input: y.real should be amplified by ~(1+eps/2)
        const auto y = ch.processOne({1.f, 0.f});
        expect(approx(y.real(), 1.1f, 0.02f));
    };

    "EVM increases with phase imbalance"_test = [] {
        float       prevEVM = 0.f;
        const float s45     = 1.f / std::sqrt(2.f);
        for (float phi : {1.f, 5.f, 10.f}) {
            gr::incubator::channel::IQImbalanceChannel<float> ch;
            ch.phase_imbalance_deg = phi;
            ch.amplitude_imbalance = 0.f;
            std::mt19937                rng{11u};
            std::bernoulli_distribution bd;
            constexpr int               N      = 200;
            float                       refPow = 0.f, errPow = 0.f;
            for (int i = 0; i < N; ++i) {
                const float               si   = bd(rng) ? s45 : -s45;
                const float               sq   = bd(rng) ? s45 : -s45;
                const std::complex<float> ref  = {si, sq};
                const std::complex<float> recv = ch.processOne(ref);
                refPow += std::norm(ref);
                errPow += std::norm(recv - ref);
            }
            const float evm = std::sqrt(errPow / refPow);
            expect(gt(evm, prevEVM)) << std::format("EVM should increase: phi={:.1f} evm={:.4f} prevEVM={:.4f}", phi, evm, prevEVM);
            prevEVM = evm;
        }
    };
};

const boost::ut::suite<"IQImbalanceChannel extended"> iqImbalanceChannelExtTests = [] {
    "zero imbalance is identity"_test = [] {
        gr::incubator::channel::IQImbalanceChannel<float> ch;
        ch.amplitude_imbalance = 0.f;
        ch.phase_imbalance_deg = 0.f;

        for (auto in : std::vector<std::complex<float>>{{1.f, 0.f}, {0.f, 1.f}, {0.7f, 0.7f}}) {
            const auto y = ch.processOne(in);
            expect(approx(y.real(), in.real(), 1e-5f));
            expect(approx(y.imag(), in.imag(), 1e-5f));
        }
    };

    "non-zero amplitude imbalance changes I/Q power ratio"_test = [] {
        gr::incubator::channel::IQImbalanceChannel<float> ch;
        ch.amplitude_imbalance = 0.2f; // 20% gain difference
        ch.phase_imbalance_deg = 0.f;

        // Input on I only: should be scaled
        const auto yi = ch.processOne({1.f, 0.f});
        // Input on Q only: should be scaled differently
        const auto yq = ch.processOne({0.f, 1.f});

        // With imbalance the magnitudes should differ
        expect(std::abs(std::abs(yi) - std::abs(yq)) > 0.05f) << std::format("|yi|={:.4f} |yq|={:.4f} should differ with amp imbalance", std::abs(yi), std::abs(yq));
    };

    "non-zero phase imbalance introduces cross-coupling"_test = [] {
        gr::incubator::channel::IQImbalanceChannel<float> ch;
        ch.amplitude_imbalance = 0.f;
        ch.phase_imbalance_deg = 10.f; // 10 degree quadrature error

        // Pure I input: without imbalance imag part would be 0; with phase error it won't be
        const auto y = ch.processOne({1.f, 0.f});
        expect(std::abs(y.imag()) > 0.01f) << std::format("phase imbalance should leak I into Q: imag={:.5f}", y.imag());
    };
};

int main() {}
