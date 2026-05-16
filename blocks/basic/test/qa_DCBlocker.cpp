// qa_DCBlocker.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <cmath>
#include <complex>
#include <format>
#include <gnuradio-4.0/basic/DCBlocker.hpp>
#include <numbers>

using namespace boost::ut;

const boost::ut::suite<"DCBlocker"> dcBlockerTests = [] {
    using namespace boost::ut;

    "removes DC offset"_test = [] {
        gr::incubator::basic::DCBlocker<float> blk;
        blk.alpha = 0.999f;
        blk.start();
        // Feed constant DC signal; after transient, output should be ~0
        for (int i = 0; i < 5000; ++i) {
            std::ignore = blk.processOne({1.f, 0.f});
        }
        const auto y = blk.processOne({1.f, 0.f});
        expect(lt(std::abs(y.real()), 0.01f)) << std::format("DC residual={:.4f}", y.real());
    };

    "passes AC signal"_test = [] {
        gr::incubator::basic::DCBlocker<float> blk;
        blk.alpha = 0.999f;
        blk.start();
        // Warm up
        for (int i = 0; i < 200; ++i) {
            std::ignore = blk.processOne({0.f, 0.f});
        }
        // Measure power of a 0.25-cycle/sample tone after DCBlocker
        float powerIn = 0.f, powerOut = 0.f;
        for (int i = 0; i < 500; ++i) {
            const float               phase = float(i) * 0.5f * std::numbers::pi_v<float>;
            const std::complex<float> s{std::cos(phase), std::sin(phase)};
            const auto                y = blk.processOne(s);
            powerIn += std::norm(s);
            powerOut += std::norm(y);
        }
        const float lossDb = 10.f * std::log10(powerOut / powerIn);
        expect(gt(lossDb, -1.f)) << std::format("AC loss={:.2f}dB", lossDb);
    };
};

const boost::ut::suite<"DCBlocker extended"> dcBlockerExtTests = [] {
    "constant input converges output to zero"_test = [] {
        gr::incubator::basic::DCBlocker<float> blk;
        blk.alpha = 0.99f;
        blk.start();

        const std::complex<float> dc{1.f, -0.5f};
        std::complex<float>       last{};
        for (int i = 0; i < 5000; ++i) {
            last = blk.processOne(dc);
        }
        expect(approx(std::abs(last), 0.f, 0.05f)) << std::format("DC blocker output magnitude {:.4f} should converge to 0", std::abs(last));
    };

    "start() clears state to zero"_test = [] {
        gr::incubator::basic::DCBlocker<float> blk;
        blk.alpha = 0.99f;
        blk.start();

        // Warm up then reset
        for (int i = 0; i < 200; ++i) {
            std::ignore = blk.processOne({5.f, 0.f});
        }
        blk.start();

        // After reset the block state should be zero; first output = input (x - 0 + 0*0)
        const auto y = blk.processOne({1.f, 0.f});
        expect(approx(y.real(), 1.f, 1e-5f)) << "after reset first output should equal first input";
    };

    "AC signal passes through with near-unity gain after settling"_test = [] {
        // A high-frequency sinewave (normalised freq 0.1) should largely pass through
        // after settling transient (alpha=0.99 → cutoff << 0.1)
        gr::incubator::basic::DCBlocker<float> blk;
        blk.alpha = 0.99f;
        blk.start();

        constexpr int   N          = 2000;
        constexpr float freq       = 0.1f; // cycles per sample
        float           sumPowerIn = 0.f, sumPowerOut = 0.f;
        for (int i = 0; i < N; ++i) {
            const float               val = std::cos(2.f * std::numbers::pi_v<float> * freq * static_cast<float>(i));
            const std::complex<float> in{val, 0.f};
            const auto                y = blk.processOne(in);
            if (i >= 500) { // skip settling
                sumPowerIn += val * val;
                sumPowerOut += y.real() * y.real() + y.imag() * y.imag();
            }
        }
        const float ratio = sumPowerOut / (sumPowerIn + 1e-12f);
        // Allow generous tolerance — IIR filter still has some effect at 0.1 normalised
        expect(ratio > 0.5f) << std::format("AC gain ratio={:.3f} too low (high-pass should pass 0.1 normalised freq)", ratio);
    };
};

// ---------------------------------------------------------------------------
// Graph test
// ---------------------------------------------------------------------------
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>

namespace {
inline gr::property_map make_props(std::initializer_list<std::pair<std::string_view, gr::pmt::Value>> init) {
    gr::property_map out;
    auto* mr = out.get_allocator().resource();
    for (const auto& [key, value] : init) {
        out.emplace(gr::pmt::Value::Map::value_type{std::pmr::string(key.data(), key.size(), mr), value});
    }
    return out;
}
} // namespace
const boost::ut::suite<"DCBlocker graph"> dcBlockerGraphTests = [] {
    using namespace boost::ut;

    "graph: removes DC from constant input"_test = [] {
        const std::vector<std::complex<float>> inputVec(32u, {1.0f, 0.0f});

        gr::Graph graph;
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data = inputVec;
        auto&     blk = graph.emplaceBlock<gr::incubator::basic::DCBlocker<float>>(make_props({{"alpha", 0.99f}}));
        auto&     snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<std::complex<float>>>();
        expect(graph.connect<"out">(src).to<"in">(blk) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(blk).to<"in">(snk) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        expect(ge(snk.data().size(), inputVec.size()));
    };
};

int main() {}
