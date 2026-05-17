// qa_PeakDetector.cpp
#include <boost/ut.hpp>
#include <cmath>
#include <gnuradio-4.0/basic/PeakDetector.hpp>
#include <vector>

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

using namespace boost::ut;

const boost::ut::suite<"PeakDetector"> peakTests = [] {
    "single clear peak detected"_test = [] {
        gr::incubator::basic::PeakDetector<float> blk;
        blk.threshold = 0.5f;
        blk.min_gap   = gr::Size_t{0u};
        blk.init(blk.progress);

        // Feed: 0, 0.3 (below threshold), 0.8 (peak), 0.3, 0
        // Peak should appear at position of 0.8 (output delayed 1 sample)
        std::vector<float>   in = {0.f, 0.3f, 0.8f, 0.3f, 0.f};
        std::vector<uint8_t> out;
        for (auto v : in) {
            out.push_back(blk.processOne(v));
        }

        // The peak at 0.8 is detected when the NEXT sample (0.3) is seen
        // so it appears at output index 3
        int peakCount = 0;
        for (auto v : out) {
            if (v) {
                ++peakCount;
            }
        }
        expect(peakCount == 1) << "exactly one peak should be detected";
    };

    "sample below threshold not detected"_test = [] {
        gr::incubator::basic::PeakDetector<float> blk;
        blk.threshold = 1.f;
        blk.init(blk.progress);

        // Feed values all below threshold
        for (int i = 0; i < 20; ++i) {
            expect(blk.processOne(0.5f) == 0u) << "below-threshold sample should not trigger";
        }
    };

    "no false trigger on plateau"_test = [] {
        gr::incubator::basic::PeakDetector<float> blk;
        blk.threshold = 0.5f;
        blk.init(blk.progress);

        // Plateau — not a strict local maximum
        int peakCount = 0;
        for (int i = 0; i < 10; ++i) {
            if (blk.processOne(1.f)) {
                ++peakCount;
            }
        }
        expect(peakCount == 0) << "plateau should not produce any peaks";
    };

    "min_gap suppresses second peak"_test = [] {
        gr::incubator::basic::PeakDetector<float> blk;
        blk.threshold = 0.5f;
        blk.min_gap   = gr::Size_t{5u};
        blk.init(blk.progress);

        // Two peaks very close together
        std::vector<float> in = {
            0.f, 0.8f, 0.f, // first peak at 0.8
            0.f, 0.8f, 0.f  // second peak — should be suppressed by min_gap
        };

        int peakCount = 0;
        for (auto v : in) {
            if (blk.processOne(v)) {
                ++peakCount;
            }
        }
        expect(peakCount <= 1) << "min_gap should suppress closely-spaced peaks";
    };

    "multiple well-separated peaks all detected"_test = [] {
        gr::incubator::basic::PeakDetector<float> blk;
        blk.threshold = 0.5f;
        blk.min_gap   = gr::Size_t{0u};
        blk.init(blk.progress);

        // 3 well-separated peaks
        std::vector<float> in = {0.f, 0.9f, 0.f, 0.f, 0.f, 0.9f, 0.f, 0.f, 0.f, 0.9f, 0.f, 0.f};

        int peakCount = 0;
        for (auto v : in) {
            if (blk.processOne(v)) {
                ++peakCount;
            }
        }
        expect(peakCount == 3) << "three separated peaks should all be detected";
    };
};

const boost::ut::suite<"PeakDetector graph"> peakDetectorGraphTests = [] {
    using namespace boost::ut;

    "graph: detects peak in stream"_test = [] {
        const std::vector<float> inputVec = {0.f, 0.3f, 0.9f, 0.3f, 0.f, 0.f, 0.f, 0.f};

        gr::Graph graph;
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<float>>();
        src.data = inputVec;
        auto&     blk = graph.emplaceBlock<gr::incubator::basic::PeakDetector<float>>(make_props({{"threshold", 0.5f}}));
        auto&     snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<uint8_t>>();
        expect(graph.connect<"out", "in">(src, blk).has_value());
        expect(graph.connect<"out", "in">(blk, snk).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const auto& out = snk.data();
        expect(ge(out.size(), inputVec.size()));
        // Exactly one peak should be detected
        int peaks = 0;
        for (const auto v : out) {
            if (v) {
                ++peaks;
            }
        }
        expect(eq(peaks, 1));
    };
};

int main() {}
