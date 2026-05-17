// qa_StaticCast.cpp
#include <boost/ut.hpp>
#include <cstdint>
#include <gnuradio-4.0/basic/StaticCast.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>

using namespace boost::ut;

const boost::ut::suite<"StaticCast"> staticCastTests = [] {
    "int16_t to float preserves value"_test = [] {
        gr::incubator::basic::StaticCast<int16_t, float> blk;
        blk.init(blk.progress);
        expect(eq(blk.processOne(int16_t{42}), 42.f));
        expect(eq(blk.processOne(int16_t{-1}), -1.f));
        expect(eq(blk.processOne(int16_t{0}), 0.f));
    };

    "uint8_t to int16_t preserves value"_test = [] {
        gr::incubator::basic::StaticCast<uint8_t, int16_t> blk;
        blk.init(blk.progress);
        expect(eq(blk.processOne(uint8_t{0}), int16_t{0}));
        expect(eq(blk.processOne(uint8_t{255}), int16_t{255}));
        expect(eq(blk.processOne(uint8_t{128}), int16_t{128}));
    };

    "float to int32_t truncates toward zero"_test = [] {
        gr::incubator::basic::StaticCast<float, int32_t> blk;
        blk.init(blk.progress);
        expect(eq(blk.processOne(3.7f), int32_t{3}));
        expect(eq(blk.processOne(-2.9f), int32_t{-2}));
        expect(eq(blk.processOne(0.f), int32_t{0}));
    };

    "int32_t to double is exact for small integers"_test = [] {
        gr::incubator::basic::StaticCast<int32_t, double> blk;
        blk.init(blk.progress);
        expect(eq(blk.processOne(int32_t{100}), 100.0));
        expect(eq(blk.processOne(int32_t{-50}), -50.0));
    };

    "identity cast int32_t to int32_t"_test = [] {
        gr::incubator::basic::StaticCast<int32_t, int32_t> blk;
        blk.init(blk.progress);
        expect(eq(blk.processOne(int32_t{12345}), int32_t{12345}));
        expect(eq(blk.processOne(int32_t{-99}), int32_t{-99}));
    };
};

const boost::ut::suite<"StaticCast graph"> staticCastGraphTests = [] {
    "graph: int16_t to float stream"_test = [] {
        const std::vector<int16_t> inputVec = {0, 1, -1, 100, -32768, 32767};

        gr::Graph graph;
        auto& src = graph.emplaceBlock<gr::incubator::basic::VectorSource<int16_t>>();
        src.data = inputVec;
        auto& blk = graph.emplaceBlock<gr::incubator::basic::StaticCast<int16_t, float>>();
        auto& snk = graph.emplaceBlock<gr::incubator::basic::VectorSink<float>>();
        expect(graph.connect<"out">(src).to<"in">(blk) == gr::ConnectionResult::SUCCESS);
        expect(graph.connect<"out">(blk).to<"in">(snk) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const auto& out = snk.data();
        expect(ge(out.size(), inputVec.size()));
        for (std::size_t i = 0; i < inputVec.size(); ++i) {
            expect(eq(out[i], static_cast<float>(inputVec[i])));
        }
    };
};

int main() {}
