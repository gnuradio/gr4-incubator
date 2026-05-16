// qa_PowerEstimator.cpp — per-block functional tests
#include <gnuradio-4.0/measure/PowerEstimator.hpp>
#include <boost/ut.hpp>
#include <complex>
#include <format>
#include <numbers>

using namespace boost::ut;
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>

const boost::ut::suite<"PowerEstimator"> powerEstTests = [] {
    using namespace boost::ut;

    "converges to correct power"_test = [] {
        gr::incubator::measure::PowerEstimator<float> pe;
        pe.alpha = 0.05f;
        pe.start();
        // Feed constant amplitude = 2  →  power should settle near 4
        for (int i = 0; i < 1000; ++i) { std::ignore = pe.processOne({2.f, 0.f}); }
        const float p = pe.processOne({2.f, 0.f});
        expect(approx(p, 4.f, 0.1f)) << std::format("power={:.3f}", p);
    };

    "dB mode near 0 dBFS"_test = [] {
        gr::incubator::measure::PowerEstimator<float> pe;
        pe.alpha = 0.1f;
        pe.output_db = true;
        pe.start();
        for (int i = 0; i < 2000; ++i) { std::ignore = pe.processOne({1.f, 0.f}); }
        const float db = pe.processOne({1.f, 0.f});
        expect(approx(db, 0.f, 0.5f)) << std::format("dB={:.2f}", db);
    };

    "no log of zero"_test = [] {
        gr::incubator::measure::PowerEstimator<float> pe;
        pe.output_db = true;
        pe.start();
        for (int i = 0; i < 100; ++i) {
            const float v = pe.processOne({0.f, 0.f});
            expect(std::isfinite(v));
        }
    };
};

const boost::ut::suite<"PowerEstimator extended"> powerEstimatorExtTests = [] {
    "unit complex input power_linear converges to 1.0"_test = [] {
        gr::incubator::measure::PowerEstimator<float> est;
        est.alpha     = 0.05f;
        est.output_db = false;
        est.start();
        const std::complex<float> in{1.f / std::numbers::sqrt2_v<float>,
                                      1.f / std::numbers::sqrt2_v<float>};
        float last = 0.f;
        for (int i = 0; i < 500; ++i) { last = est.processOne(in); }
        expect(approx(last, 1.f, 0.05f))
            << std::format("linear power {:.4f} should converge to 1.0", last);
    };

    "dB mode converges to 0 dB for unit input"_test = [] {
        gr::incubator::measure::PowerEstimator<float> est;
        est.alpha     = 0.05f;
        est.output_db = true;
        est.start();
        const std::complex<float> in{1.f, 0.f};
        float last = -99.f;
        for (int i = 0; i < 500; ++i) { last = est.processOne(in); }
        expect(approx(last, 0.f, 0.5f))
            << std::format("dB power {:.4f} should converge to 0 dB", last);
    };

    "start() resets IIR state to zero"_test = [] {
        gr::incubator::measure::PowerEstimator<float> est;
        est.alpha     = 0.5f;
        est.output_db = false;
        est.start();

        // Warm up
        for (int i = 0; i < 50; ++i) { std::ignore = est.processOne({10.f, 0.f}); }

        // Reset
        est.start();

        // Immediately after reset the IIR state is 0; first output = alpha * |in|^2
        const float firstOut = est.processOne({1.f, 0.f});
        expect(approx(firstOut, 0.5f, 0.01f))
            << std::format("after reset first output={:.4f}, expected ≈ alpha*1 = 0.5", firstOut);
    };
};


const boost::ut::suite<"PowerEstimator graph"> powerEstimatorGraphTests = [] {
    using namespace boost::ut;
    "graph: VectorSource → PowerEstimator → VectorSink runs without crash"_test = [] {
        gr::Graph graph;
        std::vector<std::complex<float>> data(8u, {1.0f, 0.0f});
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data   = data;
        auto& blk = graph.emplaceBlock<gr::incubator::measure::PowerEstimator<float>>();
        auto& snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<float>>();
        expect(graph.connect<"out">(src).to<"in">(blk) == gr::ConnectionResult::SUCCESS)
            << "VectorSource → PowerEstimator";
        expect(graph.connect<"out">(blk).to<"in">(snk) == gr::ConnectionResult::SUCCESS)
            << "PowerEstimator → VectorSink";
        gr::scheduler::Simple sched;

        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(!snk.data().empty()) << "PowerEstimator should produce output";
    };
};

int main() {}
