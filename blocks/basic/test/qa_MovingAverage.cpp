// qa_MovingAverage.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <gnuradio-4.0/basic/MovingAverage.hpp>
using namespace boost::ut;

#include <complex>

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

const boost::ut::suite<"MovingAverage"> movAvgTests = [] {
    using namespace boost::ut;
    "window size 1: identity"_test = [] {
        gr::incubator::basic::MovingAverage<float> ma;
        ma.window_size = 1u;
        ma.start();
        const auto y = ma.processOne({3.f, 1.f});
        expect(std::abs(y - std::complex<float>{3.f, 1.f}) < 1e-5f);
    };
    "constant input: settles to input"_test = [] {
        gr::incubator::basic::MovingAverage<float> ma;
        ma.window_size = 4u;
        ma.start();
        std::complex<float> y;
        for (int i = 0; i < 4; ++i) {
            y = ma.processOne({2.f, 0.f});
        }
        expect(std::abs(y.real() - 2.f) < 1e-5f) << "should settle to constant input";
    };
    "averaging alternating sequence"_test = [] {
        gr::incubator::basic::MovingAverage<float> ma;
        ma.window_size = 2u;
        ma.start();
        std::ignore  = ma.processOne({1.f, 0.f});
        const auto y = ma.processOne({-1.f, 0.f});
        expect(std::abs(y.real()) < 1e-5f) << "mean of {1,-1} should be 0";
    };
};

const boost::ut::suite<"MovingAverage extended"> movingAverageExtTests = [] {
    using namespace boost::ut;

    // DC input → output equals input after window_size samples
    "DC input converges to input value after window_size samples"_test = [] {
        constexpr uint32_t             W = 4u;
        gr::incubator::basic::MovingAverage<float> ma;
        ma.window_size = W;
        ma.start();

        std::complex<float> last{};
        for (uint32_t i = 0u; i < W; ++i) {
            last = ma.processOne(std::complex<float>{3.f, 0.f});
        }
        expect(approx(last.real(), 3.f, 1e-5f));
    };

    // Step response settles in window_size samples
    "step response settles within window_size samples"_test = [] {
        constexpr uint32_t             W = 8u;
        gr::incubator::basic::MovingAverage<float> ma;
        ma.window_size = W;
        ma.start();

        for (uint32_t i = 0u; i < W; ++i) {
            std::ignore = ma.processOne(std::complex<float>{1.f, 0.f});
        }
        // After W samples of DC=1, mean must be exactly 1
        const auto settled = ma.processOne(std::complex<float>{1.f, 0.f});
        expect(approx(settled.real(), 1.f, 1e-5f));
    };

    // Output length equals input length (one output per input)
    "output count equals input count"_test = [] {
        constexpr uint32_t             W = 4u;
        gr::incubator::basic::MovingAverage<float> ma;
        ma.window_size = W;
        ma.start();

        uint32_t count = 0u;
        for (uint32_t i = 0u; i < 10u; ++i) {
            std::ignore = ma.processOne(std::complex<float>{1.f, 0.f});
            ++count;
        }
        expect(eq(count, 10u));
    };
};

const boost::ut::suite<"MovingAverage graph"> movingAverageGraphTests = [] {
    using namespace boost::ut;

    "graph: window_size=4 DC input converges"_test = [] {
        // 8 samples of constant DC=2+0j
        std::vector<std::complex<float>> inputVec(8, {2.f, 0.f});

        gr::Graph graph;
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data = inputVec;
        auto&     blk = graph.emplaceBlock<gr::incubator::basic::MovingAverage<float>>(make_props({{"window_size", 4u}}));
        auto&     snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<std::complex<float>>>();
        expect(graph.connect<"out", "in">(src, blk).has_value());
        expect(graph.connect<"out", "in">(blk, snk).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const auto& out = snk.data();
        expect(ge(out.size(), inputVec.size()));
        // After 4 samples (window full), output should equal input = 2
        expect(approx(out[7].real(), 2.f, 1e-4f));
    };
};

int main() {}
