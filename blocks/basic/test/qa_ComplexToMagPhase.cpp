// qa_ComplexToMagPhase.cpp
#include <boost/ut.hpp>
#include <cmath>
#include <complex>
#include <gnuradio-4.0/basic/ComplextoMagPhase.hpp>
#include <gnuradio-4.0/basic/MagPhasetoComplex.hpp>
#include <numbers>
#include <tuple>
#include <vector>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>

using namespace boost::ut;

namespace {
template<typename T>
bool approxEqual(T a, T b, T tol = T(1e-5)) noexcept {
    return std::abs(a - b) <= tol * (std::abs(a) + std::abs(b) + T(1e-12));
}
} // namespace

const boost::ut::suite<"ComplexToMagPhase"> complexToMagPhaseTests = [] {
    "(1,0) gives mag=1 phase=0"_test = [] {
        gr::incubator::basic::ComplexToMagPhase<float> blk;
        blk.init(blk.progress);
        const auto [mag, phase] = blk.processOne({1.f, 0.f});
        expect(approxEqual(mag, 1.f)) << "magnitude should be 1";
        expect(approxEqual(phase, 0.f)) << "phase should be 0";
    };

    "(0,1) gives mag=1 phase=pi/2"_test = [] {
        gr::incubator::basic::ComplexToMagPhase<float> blk;
        blk.init(blk.progress);
        const auto [mag, phase] = blk.processOne({0.f, 1.f});
        expect(approxEqual(mag, 1.f)) << "magnitude should be 1";
        expect(approxEqual(phase, std::numbers::pi_v<float> / 2.f)) << "phase should be pi/2";
    };

    "(-1,0) gives mag=1 phase=pi"_test = [] {
        gr::incubator::basic::ComplexToMagPhase<float> blk;
        blk.init(blk.progress);
        const auto [mag, phase] = blk.processOne({-1.f, 0.f});
        expect(approxEqual(mag, 1.f)) << "magnitude should be 1";
        expect(approxEqual(phase, std::numbers::pi_v<float>)) << "phase should be pi";
    };

    "(2,0) gives mag=2 phase=0"_test = [] {
        gr::incubator::basic::ComplexToMagPhase<float> blk;
        blk.init(blk.progress);
        const auto [mag, phase] = blk.processOne({2.f, 0.f});
        expect(approxEqual(mag, 2.f)) << "magnitude should be 2";
        expect(approxEqual(phase, 0.f)) << "phase should be 0";
    };

    "magnitude is always non-negative"_test = [] {
        gr::incubator::basic::ComplexToMagPhase<float> blk;
        blk.init(blk.progress);
        for (const std::complex<float> s : std::vector<std::complex<float>>{{-1.f, 0.f}, {0.f, -1.f}, {-1.f, -1.f}, {0.f, 0.f}}) {
            const auto [mag, phase] = blk.processOne(s);
            expect(ge(mag, 0.f)) << "magnitude must be non-negative";
        }
    };

    "phase is in [-pi, pi]"_test = [] {
        gr::incubator::basic::ComplexToMagPhase<float> blk;
        blk.init(blk.progress);
        for (const std::complex<float> s : std::vector<std::complex<float>>{{1.f, 0.f}, {0.f, 1.f}, {-1.f, 0.f}, {0.f, -1.f}, {1.f, 1.f}}) {
            const auto [mag, phase] = blk.processOne(s);
            expect(ge(phase, -std::numbers::pi_v<float>)) << "phase should be >= -pi";
            expect(le(phase, std::numbers::pi_v<float>)) << "phase should be <= pi";
        }
    };

    "round-trip with MagPhasetoComplex"_test = [] {
        gr::incubator::basic::ComplexToMagPhase<float> split;
        gr::incubator::basic::MagPhasetoComplex<float> combine;
        split.init(split.progress);
        combine.init(combine.progress);

        const std::complex<float> orig{0.6f, 0.8f};
        const auto [mag, phase]  = split.processOne(orig);
        const auto reconstructed = combine.processOne(mag, phase);
        expect(approxEqual(reconstructed.real(), orig.real())) << "round-trip real part";
        expect(approxEqual(reconstructed.imag(), orig.imag())) << "round-trip imag part";
    };
};

const boost::ut::suite<"ComplexToMagPhase graph"> complexToMagPhaseGraphTests = [] {
    "graph: complex input split to mag and phase sinks"_test = [] {
        const std::vector<std::complex<float>> inputVec = {{1.f, 0.f}, {0.f, 1.f}, {2.f, 0.f}};

        gr::Graph graph;
        auto& src     = graph.emplaceBlock<gr::incubator::basic::VectorSource<std::complex<float>>>();
        src.data      = inputVec;
        auto& blk     = graph.emplaceBlock<gr::incubator::basic::ComplexToMagPhase<float>>();
        auto& mag_snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<float>>();
        auto& phs_snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<float>>();

        expect(graph.connect<"out", "in">(src, blk).has_value());
        expect(graph.connect<"mag", "in">(blk, mag_snk).has_value());
        expect(graph.connect<"phase", "in">(blk, phs_snk).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const auto& mags   = mag_snk.data();
        const auto& phases = phs_snk.data();
        expect(ge(mags.size(), inputVec.size()));
        expect(ge(phases.size(), inputVec.size()));
        // (1,0) → mag=1, phase=0
        expect(approxEqual(mags[0], 1.f));
        expect(approxEqual(phases[0], 0.f));
        // (0,1) → mag=1, phase=pi/2
        expect(approxEqual(mags[1], 1.f));
        expect(approxEqual(phases[1], std::numbers::pi_v<float> / 2.f));
    };
};

int main() {}
