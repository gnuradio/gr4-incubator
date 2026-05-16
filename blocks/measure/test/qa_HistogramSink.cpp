// qa_HistogramSink.cpp — per-block functional tests
#include <gnuradio-4.0/measure/HistogramSink.hpp>

#include <boost/ut.hpp>
using namespace boost::ut;

#include <algorithm>
#include <complex>
#include <string>
#include <vector>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>

const boost::ut::suite<"HistogramSink"> histTests = [] {
    using namespace boost::ut;
    "bin count matches input count"_test = [] {
        gr::incubator::measure::HistogramSink<float> sink;
        sink.n_bins = 32u; sink.min_val = -2.0f; sink.max_val = 2.0f; sink.mode = "real";
        sink.start();
        const int N = 500;
        for (int i = 0; i < N; ++i) {
            sink.processOne({static_cast<float>(i % 4) - 1.5f, 0.f});
        }
        uint64_t total = 0u;
        for (auto c : sink.counts()) { total += c; }
        expect(total == static_cast<uint64_t>(N)) << "total counts = N";
    };
    "bin_edges has n_bins+1 elements"_test = [] {
        gr::incubator::measure::HistogramSink<float> sink;
        sink.n_bins = 16u; sink.start();
        expect(sink.bin_edges().size() == std::size_t{17}) << "n_bins+1 edges";
    };
};

const boost::ut::suite<"HistogramSink extended"> histExtTests = [] {
    using namespace boost::ut;
    "constant input fills a single bin"_test = [] {
        gr::incubator::measure::HistogramSink<float> blk;
        blk.n_bins  = 10u;
        blk.min_val = -2.0f;
        blk.max_val =  2.0f;
        blk.mode    = std::string("real");
        blk.start();

        for (int i = 0; i < 100; ++i) {
            blk.processOne(std::complex<float>{0.5f, 0.0f});
        }
        const auto& counts = blk.counts();
        // Bin for 0.5: idx = 10 * (0.5-(-2)) / 4 = 10*2.5/4 = 6
        int nonzero = 0;
        for (auto c : counts) { if (c > 0u) ++nonzero; }
        expect(nonzero == 1) << "expected 1 non-zero bin for constant input, got " << nonzero;
        expect(counts[6] == 100u) << "wrong bin for 0.5: " << counts[6];
    };

    "start resets histogram bins"_test = [] {
        gr::incubator::measure::HistogramSink<float> blk;
        blk.n_bins  = 8u;
        blk.min_val = -1.0f;
        blk.max_val =  1.0f;
        blk.mode    = std::string("real");
        blk.start();

        for (int i = 0; i < 50; ++i) {
            blk.processOne(std::complex<float>{0.3f, 0.0f});
        }
        blk.start();
        const auto& counts = blk.counts();
        const bool allZero = std::all_of(counts.begin(), counts.end(),
            [](uint64_t c){ return c == 0u; });
        expect(allZero) << "counts not all zero after start()";
    };

    "magnitude mode bins absolute values"_test = [] {
        gr::incubator::measure::HistogramSink<float> blk;
        blk.n_bins  = 10u;
        blk.min_val = 0.0f;
        blk.max_val = 2.0f;
        blk.mode    = std::string("magnitude");
        blk.start();

        // Samples at magnitude 1.0 and -1.0 should both fall in same bin
        for (int i = 0; i < 50; ++i) {
            blk.processOne(std::complex<float>{0.8f, 0.6f});  // |x| = 1.0
        }
        const auto& counts = blk.counts();
        // bin for magnitude 1.0: idx = 10*(1.0-0)/2 = 5
        expect(counts[5] == 50u)
            << "magnitude bin 5 should have 50 counts: " << counts[5];
    };
};


const boost::ut::suite<"HistogramSink graph"> histogramSinkGraphTests = [] {
    using namespace boost::ut;
    "graph: VectorSource connects to HistogramSink"_test = [] {
        gr::Graph graph;
        std::vector<std::complex<float>> data(4u, {1.0f, 0.0f});
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data   = data;
        auto& snk = graph.emplaceBlock<gr::incubator::measure::HistogramSink<float>>();
        expect(graph.connect<"out">(src).to<"in">(snk) == gr::ConnectionResult::SUCCESS)
            << "VectorSource → HistogramSink";
    };
};

int main() {}
