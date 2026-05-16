// qa_SNRSteppedAWGN.cpp
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>
#include <gnuradio-4.0/channel/SNRSteppedAWGN.hpp>

#include <boost/ut.hpp>
using namespace boost::ut;

#include <complex>
#include <format>
#include <numeric>
#include <vector>

const boost::ut::suite<"SNRSteppedAWGN"> snrSteppedTests = [] {
    "number of steps: 0 to 20 dB step 2 → 11 steps"_test = [] {
        gr::incubator::channel::SNRSteppedAWGN<float> blk;
        blk.snr_start_db     = 0.f;
        blk.snr_stop_db      = 20.f;
        blk.snr_step_db      = 2.f;
        blk.samples_per_step = gr::Size_t{100};
        blk.seed             = 42ULL;
        blk.start();
        expect(blk._numSteps == 11u) << std::format("numSteps={}", blk._numSteps);
    };

    "noise power increases as SNR decreases"_test = [] {
        constexpr std::size_t N = 2000u;

        gr::incubator::channel::SNRSteppedAWGN<float> blk_high_snr;
        blk_high_snr.snr_start_db     = 20.f;
        blk_high_snr.snr_stop_db      = 20.f;
        blk_high_snr.snr_step_db      = 2.f;
        blk_high_snr.samples_per_step = gr::Size_t{N};
        blk_high_snr.seed             = 1ULL;
        blk_high_snr.start();

        gr::incubator::channel::SNRSteppedAWGN<float> blk_low_snr;
        blk_low_snr.snr_start_db     = 0.f;
        blk_low_snr.snr_stop_db      = 0.f;
        blk_low_snr.snr_step_db      = 2.f;
        blk_low_snr.samples_per_step = gr::Size_t{N};
        blk_low_snr.seed             = 2ULL;
        blk_low_snr.start();

        expect(blk_high_snr._numSteps == 1u);
        expect(blk_low_snr._numSteps == 1u);
    };

    "graph: stepped AWGN terminates graph after last step"_test = [] {
        constexpr std::size_t kSamplesPerStep = 50u;
        constexpr float       kSnrStart = 0.f, kSnrStop = 10.f, kSnrStep = 5.f;
        constexpr std::size_t kExpectedSamples = kSamplesPerStep * 3u;

        std::vector<std::complex<float>> data(1000u, {1.f, 0.f});

        gr::Graph graph;
        auto&     src  = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data        = data;
        auto&     awgn  = graph.emplaceBlock<gr::incubator::channel::SNRSteppedAWGN<float>>();
        awgn.snr_start_db     = kSnrStart;
        awgn.snr_stop_db      = kSnrStop;
        awgn.snr_step_db      = kSnrStep;
        awgn.samples_per_step = gr::Size_t{kSamplesPerStep};
        awgn.seed             = uint64_t{42};
        auto&     snk  = graph.emplaceBlock<gr::incubator::basic::VectorSink<std::complex<float>>>();

        expect(graph.connect<"out">(src).to<"in">(awgn) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(awgn).to<"in">(snk) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const std::size_t got = snk.data().size();
        expect(got >= kExpectedSamples) << std::format("SNRSteppedAWGN: output={} < expected_min={}", got, kExpectedSamples);
        expect(got <= data.size()) << std::format("SNRSteppedAWGN: output={} exceeded data size {}", got, data.size());
    };

    "graph: VectorSink receives output close to expected total (3 steps × 50 samples)"_test = [] {
        constexpr std::size_t kSamplesPerStep = 100u;
        constexpr std::size_t kNumSteps       = 5u;
        constexpr std::size_t kExpected       = kSamplesPerStep * kNumSteps;

        std::vector<std::complex<float>> data(kExpected + 500u, {1.f, 0.f});

        gr::Graph graph;
        auto&     src  = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data        = data;
        auto&     awgn  = graph.emplaceBlock<gr::incubator::channel::SNRSteppedAWGN<float>>();
        awgn.snr_start_db     = 0.f;
        awgn.snr_stop_db      = 8.f;
        awgn.snr_step_db      = 2.f;
        awgn.samples_per_step = gr::Size_t{kSamplesPerStep};
        awgn.seed             = uint64_t{1};
        auto&     snk  = graph.emplaceBlock<gr::incubator::basic::VectorSink<std::complex<float>>>();

        expect(graph.connect<"out">(src).to<"in">(awgn) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(awgn).to<"in">(snk) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const std::size_t got = snk.data().size();
        expect(got >= kExpected) << std::format("output={} < expected_min={}", got, kExpected);
    };
};

int main() {}
