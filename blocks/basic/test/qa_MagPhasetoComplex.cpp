// qa_MagPhasetoComplex.cpp
#include <boost/ut.hpp>
#include <cmath>
#include <complex>
#include <gnuradio-4.0/basic/MagPhasetoComplex.hpp>
#include <numbers>

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

const boost::ut::suite<"MagPhasetoComplex"> magPhaseTests = [] {
    "unit magnitude zero phase gives (1,0)"_test = [] {
        gr::incubator::basic::MagPhasetoComplex<float> blk;
        blk.init(blk.progress);
        const auto y = blk.processOne(1.f, 0.f);
        expect(approxEqual(y.real(), 1.f)) << "real part should be 1";
        expect(approx(y.imag(), 0.f, 1e-6f)) << "imag part should be 0";
    };

    "unit magnitude pi/2 phase gives (0,1)"_test = [] {
        gr::incubator::basic::MagPhasetoComplex<float> blk;
        blk.init(blk.progress);
        const auto y = blk.processOne(1.f, std::numbers::pi_v<float> / 2.f);
        expect(approx(y.real(), 0.f, 1e-6f)) << "real part should be ~0";
        expect(approxEqual(y.imag(), 1.f)) << "imag part should be ~1";
    };

    "unit magnitude pi phase gives (-1,0)"_test = [] {
        gr::incubator::basic::MagPhasetoComplex<float> blk;
        blk.init(blk.progress);
        const auto y = blk.processOne(1.f, std::numbers::pi_v<float>);
        expect(approxEqual(y.real(), -1.f)) << "real part should be ~-1";
        expect(approx(y.imag(), 0.f, 1e-6f)) << "imag part should be ~0";
    };

    "magnitude 2 zero phase gives (2,0)"_test = [] {
        gr::incubator::basic::MagPhasetoComplex<float> blk;
        blk.init(blk.progress);
        const auto y = blk.processOne(2.f, 0.f);
        expect(approxEqual(y.real(), 2.f)) << "real part should be 2";
        expect(approx(y.imag(), 0.f, 1e-6f)) << "imag part should be 0";
    };

    "zero magnitude gives zero output regardless of phase"_test = [] {
        gr::incubator::basic::MagPhasetoComplex<float> blk;
        blk.init(blk.progress);
        const auto y = blk.processOne(0.f, 1.23f);
        expect(approx(std::abs(y), 0.f, 1e-9f)) << "zero magnitude should give zero output";
    };

    "output magnitude equals input magnitude"_test = [] {
        gr::incubator::basic::MagPhasetoComplex<float> blk;
        blk.init(blk.progress);
        const float mag = 3.5f, phase = 0.7f;
        const auto  y   = blk.processOne(mag, phase);
        expect(approxEqual(std::abs(y), mag)) << "output magnitude should equal input magnitude";
    };

    "output phase equals input phase"_test = [] {
        gr::incubator::basic::MagPhasetoComplex<float> blk;
        blk.init(blk.progress);
        const float mag = 2.f, phase = 1.1f;
        const auto  y   = blk.processOne(mag, phase);
        expect(approxEqual(std::arg(y), phase)) << "output phase should equal input phase";
    };
};

const boost::ut::suite<"MagPhasetoComplex graph"> magPhaseGraphTests = [] {
    "graph: two VectorSources produce correct complex output"_test = [] {
        const std::vector<float> mags   = {1.f, 2.f, 0.f, 1.f};
        const std::vector<float> phases = {0.f, 0.f, 1.f, std::numbers::pi_v<float> / 2.f};

        gr::Graph graph;
        auto& mag_src   = graph.emplaceBlock<gr::incubator::basic::VectorSource<float>>();
        mag_src.data    = mags;
        auto& phase_src = graph.emplaceBlock<gr::incubator::basic::VectorSource<float>>();
        phase_src.data  = phases;
        auto& blk = graph.emplaceBlock<gr::incubator::basic::MagPhasetoComplex<float>>();
        auto& snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<std::complex<float>>>();

        expect(graph.connect<"out">(mag_src).to<"mag">(blk) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(phase_src).to<"phase">(blk) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(blk).to<"in">(snk) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const auto& out = snk.data();
        expect(ge(out.size(), mags.size()));
        // mag=1, phase=0 → (1, 0)
        expect(approxEqual(out[0].real(), 1.f));
        expect(approx(out[0].imag(), 0.f, 1e-6f));
        // mag=1, phase=pi/2 → (0, 1)
        expect(approx(out[3].real(), 0.f, 1e-6f));
        expect(approxEqual(out[3].imag(), 1.f));
    };
};

int main() {}
