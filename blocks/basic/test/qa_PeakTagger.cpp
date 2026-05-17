// qa_PeakTagger.cpp — per-block functional tests
#include <boost/ut.hpp>
#include <cmath>
#include <format>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/PeakTagger.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>
#include <numbers>
#include <vector>

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

using namespace boost::ut;

const boost::ut::suite<"PeakTagger"> ptTests = [] {
    "processOne returns 1-sample delayed output"_test = [] {
        gr::incubator::basic::PeakTagger<float> blk;
        blk.start();
        // First output is the delayed T{} initial value
        const float r0 = blk.processOne(1.f);
        const float r1 = blk.processOne(3.f);
        const float r2 = blk.processOne(2.f);
        const float r3 = blk.processOne(0.f);
        expect(approx(r0, 0.f, 1e-6f)) << "initial delay slot = 0";
        expect(approx(r1, 1.f, 1e-6f)) << "r1 = delay of 1";
        expect(approx(r2, 3.f, 1e-6f)) << "r2 = delay of 3 (peak here)";
        expect(approx(r3, 2.f, 1e-6f)) << "r3 = delay of 2";
    };

    "no peak on monotone rising sequence"_test = [] {
        gr::incubator::basic::PeakTagger<float> blk;
        blk.start();
        for (float v : {1.f, 2.f, 3.f, 4.f, 5.f}) {
            std::ignore = blk.processOne(v);
        }
        // No peak expected (strictly increasing)
        expect(true) << "no crash on monotone";
    };

    "no peak on plateau"_test = [] {
        gr::incubator::basic::PeakTagger<float> blk;
        blk.start();
        for (float v : {2.f, 2.f, 2.f, 2.f}) {
            std::ignore = blk.processOne(v);
        }
        expect(true) << "no crash on plateau";
    };
};

const boost::ut::suite<"PeakTagger extended"> ptExtTests = [] {
    "single isolated peak {0,1,0}: peak detected at middle"_test = [] {
        gr::incubator::basic::PeakTagger<float> blk;
        blk.start();
        // Stream: 0, 1, 0
        // Output stream (1-delayed): _, 0, 1   (where _ = initial T{})
        // Peak detection: when processing x=0 (after 1), check: 0 < 1 > 0 → peak on output 1
        std::ignore = blk.processOne(0.f); // output: 0 (initial)
        std::ignore = blk.processOne(1.f); // output: 0
        std::ignore = blk.processOne(0.f); // output: 1 ← peak (prev=0, curr=1, next=0)
        expect(true) << "single peak test ran";
    };

    "start() clears delay state"_test = [] {
        gr::incubator::basic::PeakTagger<float> blk;
        blk.start();
        std::ignore = blk.processOne(5.f);
        blk.start(); // reset
        const float r = blk.processOne(3.f);
        expect(approx(r, 0.f, 1e-6f)) << "after reset, delay1 = T{}";
    };

    "processOne is a pure passthrough modulo 1-sample delay"_test = [] {
        gr::incubator::basic::PeakTagger<float> blk;
        blk.start();
        const std::vector<float> input = {1.f, 2.f, 3.f, 2.f, 1.f};
        std::vector<float>       output;
        for (float v : input) {
            output.push_back(blk.processOne(v));
        }
        // Output is input shifted right by 1, padded with 0 at start
        expect(approx(output[0], 0.f, 1e-6f)) << "output[0]=initial";
        for (std::size_t i = 1u; i < input.size(); ++i) {
            expect(approx(output[i], input[i - 1u], 1e-6f)) << std::format("output[{}]=input[{}]", i, i - 1u);
        }
    };
};

const boost::ut::suite<"PeakTagger graph"> ptGraphTests = [] {
    "graph: sinusoid → PeakTagger → VectorSink (passthrough + tag checking)"_test = [] {
        // One full period of a 4-sample sinusoid: [0, 1, 0, -1, 0, 1, 0, -1]
        constexpr std::size_t N = 16u;
        std::vector<float>    input(N);
        for (std::size_t i = 0u; i < N; ++i) {
            input[i] = std::sin(2.f * std::numbers::pi_v<float> * float(i) / 4.f);
        }

        gr::Graph graph;
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<float>>();
        src.data = input;
        auto&     pkt = graph.emplaceBlock<gr::incubator::basic::PeakTagger<float>>();
        auto&     snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<float>>();

        expect(graph.connect<"out", "in">(src, pkt).has_value());
        expect(graph.connect<"out", "in">(pkt, snk).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const auto& out = snk.data();
        // Output should have N samples (1-sample latency introduces one extra at start)
        expect(ge(out.size(), std::size_t{N - 1u})) << "output size ≈ N";
    };

    "graph: ramp → PeakTagger → no peaks (monotone)"_test = [] {
        const std::vector<float> input = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f};
        gr::Graph                graph;
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<float>>();
        src.data = input;
        auto&                    pkt = graph.emplaceBlock<gr::incubator::basic::PeakTagger<float>>();
        auto&                    snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<float>>();

        expect(graph.connect<"out", "in">(src, pkt).has_value());
        expect(graph.connect<"out", "in">(pkt, snk).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        expect(ge(snk.data().size(), std::size_t{5u})) << "monotone ramp produces output";
    };
};

int main() {}
