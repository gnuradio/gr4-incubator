// qa_FrequencyOffsetChannel.cpp — per-block functional tests
#include <gnuradio-4.0/channel/FrequencyOffsetChannel.hpp>
#include <boost/ut.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <vector>

using namespace boost::ut;

namespace {
template<typename T>
bool approxEqual(T a, T b, T tol) noexcept {
    return std::abs(a - b) <= tol * (std::abs(a) + std::abs(b) + T(1e-12));
}
} // namespace

const boost::ut::suite<"FrequencyOffsetChannel"> freqOffTests = [] {
    using namespace boost::ut;

    "unit magnitude preserved"_test = [] {
        gr::incubator::channel::FrequencyOffsetChannel<float> ch;
        ch.freq_offset_norm = 0.01f;
        ch.start();
        for (int i = 0; i < 100; ++i) {
            const auto y = ch.processOne({std::cos(float(i)*0.3f), std::sin(float(i)*0.3f)});
            expect(approx(std::abs(y), 1.f, 1e-5f));
        }
    };

    "zero offset is identity"_test = [] {
        gr::incubator::channel::FrequencyOffsetChannel<float> ch;
        ch.freq_offset_norm = 0.f;
        ch.start();
        const std::complex<float> s{0.7f, -0.3f};
        expect(approx(ch.processOne(s).real(), s.real(), 1e-5f));
        expect(approx(ch.processOne(s).imag(), s.imag(), 1e-5f));
    };

    "phase advances linearly"_test = [] {
        gr::incubator::channel::FrequencyOffsetChannel<float> ch;
        ch.freq_offset_norm = 0.05f;
        ch.start();
        // Feed unit phasor at 0; track phase of output
        std::vector<float> phases;
        for (int i = 0; i < 10; ++i) {
            const auto y = ch.processOne({1.f, 0.f});
            phases.push_back(std::arg(y));
        }
        const float step = 2.f * std::numbers::pi_v<float> * 0.05f;
        for (std::size_t i = 1u; i < 10u; ++i) {
            float diff = phases[i] - phases[i-1u];
            // Unwrap
            while (diff >  std::numbers::pi_v<float>) { diff -= 2.f*std::numbers::pi_v<float>; }
            while (diff < -std::numbers::pi_v<float>) { diff += 2.f*std::numbers::pi_v<float>; }
            expect(approx(diff, step, 1e-4f)) << std::format("step[{}]={:.4f} expected {:.4f}", i, diff, step);
        }
    };
};

const boost::ut::suite<"FrequencyOffsetChannel extended"> freqOffsetExtTests = [] {
    "zero offset is identity (magnitude)"_test = [] {
        gr::incubator::channel::FrequencyOffsetChannel<float> ch;
        ch.freq_offset_norm = 0.f;
        ch.start();

        const std::complex<float> in{0.6f, 0.8f};
        bool ok = true;
        for (int i = 0; i < 20; ++i) {
            const auto y = ch.processOne(in);
            if (std::abs(std::abs(y) - std::abs(in)) > 1e-5f) { ok = false; break; }
        }
        expect(ok) << "zero offset must preserve amplitude";
    };

    "known offset rotates phase at correct rate"_test = [] {
        constexpr float offset = 0.01f;
        gr::incubator::channel::FrequencyOffsetChannel<float> ch;
        ch.freq_offset_norm = offset;
        ch.start();

        const std::complex<float> in{1.f, 0.f};
        // After n samples, accumulated phase = n * 2pi * offset
        std::complex<float> y0 = ch.processOne(in);  // phase after 1 step
        std::complex<float> y1 = ch.processOne(in);  // phase after 2 steps

        const float ph0 = std::arg(y0);
        const float ph1 = std::arg(y1);
        const float diff = ph1 - ph0;
        const float expected = 2.f * std::numbers::pi_v<float> * offset;

        // diff may wrap; check mod 2pi
        float diffNorm = diff;
        while (diffNorm >  std::numbers::pi_v<float>) { diffNorm -= 2.f * std::numbers::pi_v<float>; }
        while (diffNorm < -std::numbers::pi_v<float>) { diffNorm += 2.f * std::numbers::pi_v<float>; }

        expect(approxEqual(diffNorm, expected, 1e-3f))
            << std::format("phase step={:.5f} expected={:.5f}", diffNorm, expected);
    };

    "start() resets phase accumulator"_test = [] {
        gr::incubator::channel::FrequencyOffsetChannel<float> ch;
        ch.freq_offset_norm = 0.1f;
        ch.start();

        const std::complex<float> in{1.f, 0.f};
        for (int i = 0; i < 50; ++i) { std::ignore = ch.processOne(in); }

        ch.start();  // reset phase to 0
        const auto y = ch.processOne(in);
        const float ph = std::arg(y);
        const float expected = 2.f * std::numbers::pi_v<float> * 0.1f;

        float phNorm = ph;
        while (phNorm >  std::numbers::pi_v<float>) { phNorm -= 2.f * std::numbers::pi_v<float>; }
        while (phNorm < -std::numbers::pi_v<float>) { phNorm += 2.f * std::numbers::pi_v<float>; }

        expect(approxEqual(phNorm, expected, 1e-4f))
            << std::format("after reset phase={:.5f} expected={:.5f}", phNorm, expected);
    };
};

int main() {}
