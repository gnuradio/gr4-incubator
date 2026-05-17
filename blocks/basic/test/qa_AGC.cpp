// qa_AGC.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <complex>
#include <format>
#include <gnuradio-4.0/basic/AGC.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>

using namespace boost::ut;

namespace {
template<typename T>
bool approxEqual(T a, T b, T tol) noexcept {
    return std::abs(a - b) <= tol * (std::abs(a) + std::abs(b) + T(1e-12));
}
} // namespace

const boost::ut::suite<"AGC"> agcTests = [] {
    "output power converges to reference"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.reference_power = 1.f;
        agc.rate            = 1e-3f;
        agc.init(agc.progress);
        // Feed 2000 samples at constant amplitude 2 (power 4)
        const std::complex<float> in{2.f, 0.f};
        std::complex<float>       last{};
        for (int i = 0; i < 2000; ++i) {
            last = agc.processOne(in);
        }
        const float power = last.real() * last.real() + last.imag() * last.imag();
        expect(approx(power, 1.f, 0.05f)) << std::format("final power {:.4f} not near 1.0", power);
    };

    "tracks step-up in amplitude"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.init(agc.progress);
        for (int i = 0; i < 500; ++i) {
            std::ignore = agc.processOne({1.f, 0.f});
        }
        const float gainAfterLow = agc._gain;
        for (int i = 0; i < 500; ++i) {
            std::ignore = agc.processOne({2.f, 0.f});
        }
        expect(lt(agc._gain, gainAfterLow)) << "gain should decrease when amplitude doubles";
    };

    "tracks step-down in amplitude"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.init(agc.progress);
        for (int i = 0; i < 500; ++i) {
            std::ignore = agc.processOne({2.f, 0.f});
        }
        const float gainAfterHigh = agc._gain;
        for (int i = 0; i < 500; ++i) {
            std::ignore = agc.processOne({0.5f, 0.f});
        }
        expect(gt(agc._gain, gainAfterHigh)) << "gain should increase when amplitude halves";
    };

    "gain clamped to max_gain"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.max_gain = 2.f;
        agc.init(agc.progress);
        for (int i = 0; i < 10000; ++i) {
            std::ignore = agc.processOne({0.f, 0.f});
        }
        expect(le(agc._gain, 2.f)) << std::format("_gain={:.4f} exceeded max_gain=2", agc._gain);
    };

    "gain clamped to min_gain"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.min_gain = 0.5f;
        agc.rate     = 0.1f;
        agc.init(agc.progress);
        for (int i = 0; i < 10000; ++i) {
            std::ignore = agc.processOne({100.f, 0.f});
        }
        expect(ge(agc._gain, 0.5f)) << std::format("_gain={:.4f} below min_gain=0.5", agc._gain);
    };

    "zero rate means constant gain"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.rate = 0.f;
        agc.init(agc.progress);
        for (int i = 0; i < 200; ++i) {
            std::ignore = agc.processOne({4.f, 0.f});
        }
        expect(approx(agc._gain, 1.f, 1e-6f)) << "gain must not change when rate=0";
    };
};

const boost::ut::suite<"AGC extended"> agcExtTests = [] {
    "low input power causes gain to increase"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.reference_power = 1.f;
        agc.rate            = 1e-3f;
        agc.init(agc.progress);

        // Start from gain=1; feed very small amplitude (power << 1) → gain increases
        const float gainBefore = agc._gain;
        for (int i = 0; i < 500; ++i) {
            std::ignore = agc.processOne({0.01f, 0.f});
        }
        expect(gt(agc._gain, gainBefore)) << std::format("gain should increase for low-power input: before={:.4f} after={:.4f}", gainBefore, agc._gain);
    };

    "high input power causes gain to decrease"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.reference_power = 1.f;
        agc.rate            = 1e-3f;
        agc.init(agc.progress);

        const float gainBefore = agc._gain;
        for (int i = 0; i < 500; ++i) {
            std::ignore = agc.processOne({10.f, 0.f});
        }
        expect(lt(agc._gain, gainBefore)) << std::format("gain should decrease for high-power input: before={:.4f} after={:.4f}", gainBefore, agc._gain);
    };

    "output power converges to reference_power"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.reference_power = 4.f;
        agc.rate            = 5e-4f;
        agc.max_gain        = 1000.f;
        agc.init(agc.progress);

        const std::complex<float> in{0.1f, 0.f}; // power = 0.01 → gain must rise
        std::complex<float>       last{};
        for (int i = 0; i < 20000; ++i) {
            last = agc.processOne(in);
        }
        const float outPow = last.real() * last.real() + last.imag() * last.imag();
        expect(approxEqual(outPow, 4.f, 0.1f)) << std::format("output power {:.4f} should converge to reference 4.0", outPow);
    };

    "start() resets gain to 1"_test = [] {
        gr::incubator::basic::AGC<float> agc;
        agc.rate = 0.1f;
        agc.init(agc.progress);

        for (int i = 0; i < 200; ++i) {
            std::ignore = agc.processOne({5.f, 0.f});
        }
        agc.start();
        expect(approxEqual(agc._gain, 1.f, 1e-6f)) << std::format("after start() _gain={:.6f} should be reset to 1", agc._gain);
    };
};

// ---------------------------------------------------------------------------
// Graph test
// ---------------------------------------------------------------------------
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

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

const boost::ut::suite<"AGC graph"> agcGraphTests = [] {
    using namespace boost::ut;

    "graph: runs without error and outputs correct size"_test = [] {
        const std::vector<std::complex<float>> inputVec(16, {1.0f, 0.0f});

        gr::Graph graph;
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data = inputVec;
        auto&     blk = graph.emplaceBlock<gr::incubator::basic::AGC<float>>(make_props({{"reference_power", 1.0f}, {"rate", 1e-3f}}));
        auto&     snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<std::complex<float>>>();
        expect(graph.connect<"out", "in">(src, blk).has_value());
        expect(graph.connect<"out", "in">(blk, snk).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        expect(ge(snk.data().size(), inputVec.size()));
    };
};

int main() {}
