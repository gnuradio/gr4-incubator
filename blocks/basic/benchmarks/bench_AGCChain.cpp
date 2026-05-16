// bench_AGCChain.cpp — AGC + carrier recovery pipeline throughput
// Chain: VectorSource → MultiplyConst(0.05) → AGC → CostasLoop → VectorSink
#include <gnuradio-4.0/basic/AGC.hpp>
#include <gnuradio-4.0/basic/MultiplyConst.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/sync/CostasLoop.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

int main() {
    constexpr std::size_t N_IN = 1'000'000UZ;
    constexpr float kA = 0.70710678118f;

    std::vector<std::complex<float>> input(N_IN);
    for (std::size_t i = 0u; i < N_IN; ++i) {
        const float re = (i & 1u) ? kA : -kA;
        const float im = (i & 2u) ? kA : -kA;
        input[i] = {re, im};
    }

    gr::Graph graph;
    auto& src  = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>({{"data", input}});
    auto& att  = graph.emplaceBlock<gr::incubator::basic::MultiplyConst<std::complex<float>>>(
        gr::property_map{{"k", 0.05f}});
    auto& agc  = graph.emplaceBlock<gr::incubator::basic::AGC<float>>(
        gr::property_map{{"reference_power", 1.f}, {"rate", 1e-3f},
                         {"max_gain", 100.f},      {"min_gain", 1e-4f}});
    auto& loop = graph.emplaceBlock<gr::incubator::basic::CostasLoop<float>>(
        gr::property_map{{"loop_bw", 0.01f}, {"damping", 0.707f}});
    auto& snk  = graph.emplaceBlock<gr::incubator::basic::VectorSink<std::complex<float>>>({});

    graph.connect<"out">(src).to<"in">(att);
    graph.connect<"out">(att).to<"in">(agc);
    graph.connect<"out">(agc).to<"in">(loop);
    graph.connect<"out">(loop).to<"in">(snk);

    gr::scheduler::Simple sched;
    sched.exchange(std::move(graph));

    const auto t0 = std::chrono::steady_clock::now();
    sched.runAndWait();
    const auto t1 = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(t1 - t0).count();

    const std::size_t nOut = snk.data().size();
    std::printf("MultiplyConst + AGC + CostasLoop: %.2f MSamples/s (output=%zu from %zu input)\n",
                static_cast<double>(nOut) / dt / 1e6,
                nOut, N_IN);
}
