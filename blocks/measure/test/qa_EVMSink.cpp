// qa_EVMSink.cpp
#include <gnuradio-4.0/measure/EVMSink.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>

#include <boost/ut.hpp>
using namespace boost::ut;

#include <cmath>
#include <complex>
#include <random>
#include <vector>

const boost::ut::suite<"EVMSink"> evmTests = [] {
    "perfect symbols → evm_rms near zero"_test = [] {
        gr::incubator::measure::EVMSink<float> sink;
        sink.start();
        std::vector<std::complex<float>> recv(100), ref(100);
        for (int i = 0; i < 100; ++i) {
            ref[i] = recv[i] = {(i % 2 == 0) ? 1.f : -1.f, 0.f};
        }
        std::ignore = sink.processBulk(
            std::span<const std::complex<float>>(recv),
            std::span<const std::complex<float>>(ref));
        expect(sink.evm_rms() < 0.01f) << "perfect: evm_rms=" << sink.evm_rms();
        expect(sink.n_symbols() == 100u);
    };

    "evm_rms increases with noise"_test = [] {
        float prev = 0.f;
        for (float sigma : {0.05f, 0.15f, 0.3f}) {
            gr::incubator::measure::EVMSink<float> sink;
            sink.start();
            std::mt19937 rng{5u};
            std::normal_distribution<float> g(0.f, sigma);
            std::bernoulli_distribution bd(0.5);
            constexpr int N = 1000;
            std::vector<std::complex<float>> recv(N), ref(N);
            for (int i = 0; i < N; ++i) {
                const float s = bd(rng) ? 1.f : -1.f;
                ref[i]  = {s, 0.f};
                recv[i] = {s + g(rng), g(rng)};
            }
            std::ignore = sink.processBulk(
                std::span<const std::complex<float>>(recv),
                std::span<const std::complex<float>>(ref));
            expect(sink.evm_rms() > prev) << "σ=" << sigma << " evm=" << sink.evm_rms();
            prev = sink.evm_rms();
        }
    };

    "known offset gives known EVM"_test = [] {
        // offset all received by 0.1 from reference → EVM% ≈ 10%
        gr::incubator::measure::EVMSink<float> sink;
        sink.start();
        constexpr int N = 1000;
        std::vector<std::complex<float>> recv(N), ref(N);
        for (int i = 0; i < N; ++i) {
            const float s = (i % 2 == 0) ? 1.f : -1.f;
            ref[i]  = {s, 0.f};
            recv[i] = {s + 0.1f, 0.f};
        }
        std::ignore = sink.processBulk(
            std::span<const std::complex<float>>(recv),
            std::span<const std::complex<float>>(ref));
        // EVM% = 100 * sqrt(N*0.01 / N*1.0) = 10%
        expect(std::abs(sink.evm_rms() - 10.f) < 0.5f) << "evm=" << sink.evm_rms();
    };

    "n_symbols() counts all processed pairs"_test = [] {
        gr::incubator::measure::EVMSink<float> sink;
        sink.start();
        std::vector<std::complex<float>> v(200, {1.f, 0.f});
        std::ignore = sink.processBulk(
            std::span<const std::complex<float>>(v),
            std::span<const std::complex<float>>(v));
        expect(sink.n_symbols() == 200u);
    };

    "evm_peak >= evm_rms"_test = [] {
        gr::incubator::measure::EVMSink<float> sink;
        sink.start();
        std::mt19937 rng{7u};
        std::normal_distribution<float> g(0.f, 0.2f);
        constexpr int N = 500;
        std::vector<std::complex<float>> recv(N), ref(N);
        for (int i = 0; i < N; ++i) {
            ref[i]  = {1.f, 0.f};
            recv[i] = {1.f + g(rng), g(rng)};
        }
        std::ignore = sink.processBulk(
            std::span<const std::complex<float>>(recv),
            std::span<const std::complex<float>>(ref));
        expect(sink.evm_peak() >= sink.evm_rms());
    };
};

const boost::ut::suite<"EVMSink extended"> evmExtTests = [] {
    "start() resets all accumulators"_test = [] {
        gr::incubator::measure::EVMSink<float> sink;
        sink.start();
        std::vector<std::complex<float>> recv = {{2.f,2.f},{3.f,3.f}};
        std::vector<std::complex<float>> ref  = {{1.f,0.f},{1.f,0.f}};
        std::ignore = sink.processBulk(
            std::span<const std::complex<float>>(recv),
            std::span<const std::complex<float>>(ref));
        expect(sink.n_symbols() > 0u);

        sink.start();
        expect(sink.n_symbols() == 0u);
        expect(sink.evm_rms()  == 0.f);
        expect(sink.evm_peak() == 0.f);
    };

    "zero noise: evm_rms is effectively zero"_test = [] {
        gr::incubator::measure::EVMSink<float> sink;
        sink.start();
        constexpr int N = 200;
        std::vector<std::complex<float>> v(N);
        for (int i = 0; i < N; ++i) {
            v[i] = {(i % 2 == 0) ? 1.f : -1.f, 0.f};
        }
        std::ignore = sink.processBulk(
            std::span<const std::complex<float>>(v),
            std::span<const std::complex<float>>(v));
        expect(sink.evm_rms() < 0.001f) << "evm_rms=" << sink.evm_rms();
        expect(sink.evm_peak() < 0.001f);
    };

    "QPSK reference produces correct EVM"_test = [] {
        const float s = 1.f / std::sqrt(2.f);
        gr::incubator::measure::EVMSink<float> sink;
        sink.constellation = std::string{"qpsk"};
        sink.start();
        constexpr int N = 400;
        std::vector<std::complex<float>> recv(N), ref(N);
        const std::complex<float> pts[4] = {{s,s},{-s,s},{s,-s},{-s,-s}};
        for (int i = 0; i < N; ++i) {
            ref[i] = recv[i] = pts[i % 4];
        }
        std::ignore = sink.processBulk(
            std::span<const std::complex<float>>(recv),
            std::span<const std::complex<float>>(ref));
        expect(sink.evm_rms() < 0.001f);
    };

    "accumulates across multiple processBulk calls"_test = [] {
        gr::incubator::measure::EVMSink<float> sink;
        sink.start();
        std::vector<std::complex<float>> v = {{1.f,0.f}};
        for (int i = 0; i < 50; ++i) {
            std::ignore = sink.processBulk(
                std::span<const std::complex<float>>(v),
                std::span<const std::complex<float>>(v));
        }
        expect(sink.n_symbols() == 50u);
    };
};

const boost::ut::suite<"EVMSink graph"> evmGraphTests = [] {
    "graph: two VectorSources connect to EVMSink"_test = [] {
        gr::Graph graph;
        std::vector<std::complex<float>> recv = {{1.f,0.f},{-1.f,0.f},{1.f,0.f},{-1.f,0.f}};
        std::vector<std::complex<float>> ref  = {{1.f,0.f},{-1.f,0.f},{1.f,0.f},{-1.f,0.f}};
        auto& src1 = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src1.data   = recv;
        auto& src2 = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src2.data   = ref;
        auto& snk  = graph.emplaceBlock<gr::incubator::measure::EVMSink<float>>();
        expect(graph.connect<"out">(src1).to<"in">(snk) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(src2).to<"reference">(snk) == gr::ConnectionResult::SUCCESS);
    };

    "graph: perfect symbols → evm_rms near zero"_test = [] {
        constexpr std::size_t N = 64u;
        std::vector<std::complex<float>> v(N);
        for (std::size_t i = 0u; i < N; ++i) {
            v[i] = {(i % 2u == 0u) ? 1.f : -1.f, 0.f};
        }
        gr::Graph graph;
        auto& src1 = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src1.data   = v;
        auto& src2 = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src2.data   = v;
        auto& snk  = graph.emplaceBlock<gr::incubator::measure::EVMSink<float>>();
        expect(graph.connect<"out">(src1).to<"in">(snk) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(src2).to<"reference">(snk) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(snk.evm_rms() < 0.01f) << "graph evm_rms=" << snk.evm_rms();
        expect(snk.n_symbols() > 0u);
    };

    "graph: noisy received gives higher EVM than perfect"_test = [] {
        constexpr std::size_t N = 256u;
        std::vector<std::complex<float>> recv(N), ref(N);
        std::mt19937 rng{42u};
        std::normal_distribution<float> noise(0.f, 0.2f);
        for (std::size_t i = 0u; i < N; ++i) {
            const float s = (i % 2u == 0u) ? 1.f : -1.f;
            ref[i]  = {s, 0.f};
            recv[i] = {s + noise(rng), noise(rng)};
        }

        gr::Graph graph;
        auto& src1 = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src1.data   = recv;
        auto& src2 = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src2.data   = ref;
        auto& snk  = graph.emplaceBlock<gr::incubator::measure::EVMSink<float>>();
        expect(graph.connect<"out">(src1).to<"in">(snk) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(src2).to<"reference">(snk) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(snk.evm_rms() > 1.f) << "noisy graph should have non-trivial EVM";
        expect(snk.n_symbols() > 0u) << "n_symbols=" << snk.n_symbols();
    };
};

int main() {}
