// qa_RicianFadingChannel.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <gnuradio-4.0/channel/RicianFadingChannel.hpp>
using namespace boost::ut;

#include <cmath>
#include <complex>
#include <vector>

const boost::ut::suite<"RicianFadingChannel"> ricianFadingChannelTests = [] {
    using namespace boost::ut;

    "K=0 gives Rayleigh (nonzero variance)"_test = [] {
        gr::incubator::channel::RicianFadingChannel<float> ch;
        ch.k_factor         = 0.0f;
        ch.snr_db           = 40.0f;
        ch.max_doppler_norm = 0.05f;
        ch.seed             = 1u;
        ch.start();
        std::vector<float> amps;
        for (int i = 0; i < 2000; ++i) {
            amps.push_back(std::abs(ch.processOne(std::complex<float>{1.f, 0.f})));
        }
        float mean = 0.f;
        for (float v : amps) { mean += v; }
        mean /= static_cast<float>(amps.size());
        float variance = 0.f;
        for (float v : amps) { variance += (v - mean) * (v - mean); }
        variance /= static_cast<float>(amps.size());
        expect(variance > 0.05f) << "K=0 should have nonzero variance";
    };

    "K=100 near-constant amplitude"_test = [] {
        gr::incubator::channel::RicianFadingChannel<float> ch;
        ch.k_factor         = 100.0f;
        ch.snr_db           = 40.0f;
        ch.max_doppler_norm = 0.01f;
        ch.seed             = 2u;
        ch.start();
        std::vector<float> amps;
        for (int i = 0; i < 1000; ++i) {
            amps.push_back(std::abs(ch.processOne(std::complex<float>{1.f, 0.f})));
        }
        float mean = 0.f;
        for (float v : amps) { mean += v; }
        mean /= static_cast<float>(amps.size());
        float var = 0.f;
        for (float v : amps) { var += (v - mean) * (v - mean); }
        var /= static_cast<float>(amps.size());
        expect(var < 0.1f) << "K=100 should have small amplitude variance";
    };
};

const boost::ut::suite<"RicianFadingChannel extended"> ricianFadingChannelExtTests = [] {
    using namespace boost::ut;

    "output power near input power over many samples"_test = [] {
        gr::incubator::channel::RicianFadingChannel<float> blk;
        blk.k_factor         = 1.0f;
        blk.snr_db           = 40.0f;
        blk.max_doppler_norm = 0.001f;
        blk.seed             = 99u;
        blk.start();

        const std::complex<float> x{1.0f, 0.0f};
        float                     sumPow = 0.0f;
        constexpr int             N      = 10000;
        for (int i = 0; i < N; ++i) {
            sumPow += std::norm(blk.processOne(x));
        }
        const float meanPow = sumPow / static_cast<float>(N);
        expect(meanPow > 0.5f && meanPow < 2.0f) << "mean power out of range: " << meanPow;
    };

    "large K factor gives near-constant amplitude"_test = [] {
        gr::incubator::channel::RicianFadingChannel<float> blk;
        blk.k_factor         = 1000.0f;
        blk.snr_db           = 60.0f;
        blk.max_doppler_norm = 0.0f;
        blk.seed             = 7u;
        blk.start();

        const std::complex<float> x{1.0f, 0.0f};
        float                     minAmp = 1e10f, maxAmp = 0.0f;
        for (int i = 0; i < 200; ++i) {
            const float amp = std::abs(blk.processOne(x));
            minAmp          = std::min(minAmp, amp);
            maxAmp          = std::max(maxAmp, amp);
        }
        expect(maxAmp - minAmp < 0.5f) << "amplitude spread too large for high K: " << (maxAmp - minAmp);
    };

    "start resets channel state"_test = [] {
        gr::incubator::channel::RicianFadingChannel<float> blk;
        blk.k_factor = 1.0f;
        blk.snr_db   = 30.0f;
        blk.seed     = 42u;
        blk.start();

        const std::complex<float>        x{1.0f, 0.0f};
        std::vector<std::complex<float>> run1, run2;
        for (int i = 0; i < 20; ++i) { run1.push_back(blk.processOne(x)); }

        blk.start();
        for (int i = 0; i < 20; ++i) { run2.push_back(blk.processOne(x)); }

        expect(eq(run1[0], run2[0])) << "start() did not reset RNG state";
    };
};

int main() {}
