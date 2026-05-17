// qa_EndianSwap.cpp
#include <boost/ut.hpp>
#include <bit>
#include <cstdint>
#include <gnuradio-4.0/basic/EndianSwap.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/VectorSink.hpp>
#include <gnuradio-4.0/basic/VectorSource.hpp>

using namespace boost::ut;

const boost::ut::suite<"EndianSwap"> endianSwapTests = [] {
    "uint8_t is identity"_test = [] {
        gr::incubator::basic::EndianSwap<uint8_t> blk;
        blk.init(blk.progress);
        expect(eq(blk.processOne(uint8_t{0xAB}), uint8_t{0xAB}));
        expect(eq(blk.processOne(uint8_t{0x00}), uint8_t{0x00}));
        expect(eq(blk.processOne(uint8_t{0xFF}), uint8_t{0xFF}));
    };

    "int16_t bytes are swapped"_test = [] {
        gr::incubator::basic::EndianSwap<int16_t> blk;
        blk.init(blk.progress);
        const int16_t in       = 0x1234;
        const int16_t expected = static_cast<int16_t>(std::byteswap(static_cast<uint16_t>(in)));
        expect(eq(blk.processOne(in), expected));
    };

    "int32_t bytes are swapped"_test = [] {
        gr::incubator::basic::EndianSwap<int32_t> blk;
        blk.init(blk.progress);
        const int32_t in       = 0x12345678;
        const int32_t expected = static_cast<int32_t>(std::byteswap(static_cast<uint32_t>(in)));
        expect(eq(blk.processOne(in), expected));
    };

    "float bit pattern is reversed"_test = [] {
        gr::incubator::basic::EndianSwap<float> blk;
        blk.init(blk.progress);
        const float    in      = 1.0f;
        const float    out     = blk.processOne(in);
        const uint32_t inBits  = std::bit_cast<uint32_t>(in);
        const uint32_t outBits = std::bit_cast<uint32_t>(out);
        expect(eq(outBits, std::byteswap(inBits)));
    };

    "double bit pattern is reversed"_test = [] {
        gr::incubator::basic::EndianSwap<double> blk;
        blk.init(blk.progress);
        const double   in      = 1.0;
        const double   out     = blk.processOne(in);
        const uint64_t inBits  = std::bit_cast<uint64_t>(in);
        const uint64_t outBits = std::bit_cast<uint64_t>(out);
        expect(eq(outBits, std::byteswap(inBits)));
    };

    "int16_t double swap is identity"_test = [] {
        gr::incubator::basic::EndianSwap<int16_t> blk;
        blk.init(blk.progress);
        const int16_t val = 0x0102;
        expect(eq(blk.processOne(blk.processOne(val)), val));
    };

    "int32_t double swap is identity"_test = [] {
        gr::incubator::basic::EndianSwap<int32_t> blk;
        blk.init(blk.progress);
        const int32_t val = 0x01020304;
        expect(eq(blk.processOne(blk.processOne(val)), val));
    };

    "float double swap is identity"_test = [] {
        gr::incubator::basic::EndianSwap<float> blk;
        blk.init(blk.progress);
        const float val = 3.14159f;
        expect(eq(blk.processOne(blk.processOne(val)), val));
    };

    "double double swap is identity"_test = [] {
        gr::incubator::basic::EndianSwap<double> blk;
        blk.init(blk.progress);
        const double val = 2.718281828;
        expect(eq(blk.processOne(blk.processOne(val)), val));
    };
};

const boost::ut::suite<"EndianSwap graph"> endianSwapGraphTests = [] {
    "graph: int32_t double swap is identity"_test = [] {
        const std::vector<int32_t> inputVec = {0x01020304, 0x12345678, 0x00000001, -1};

        gr::Graph graph;
        auto& src   = graph.emplaceBlock<gr::incubator::basic::VectorSource<int32_t>>();
        src.data    = inputVec;
        auto& swap1 = graph.emplaceBlock<gr::incubator::basic::EndianSwap<int32_t>>();
        auto& swap2 = graph.emplaceBlock<gr::incubator::basic::EndianSwap<int32_t>>();
        auto& snk   = graph.emplaceBlock<gr::incubator::basic::VectorSink<int32_t>>();
        expect(graph.connect<"out", "in">(src, swap1).has_value());
        expect(graph.connect<"out", "in">(swap1, swap2).has_value());
        expect(graph.connect<"out", "in">(swap2, snk).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const auto& out = snk.data();
        expect(ge(out.size(), inputVec.size()));
        for (std::size_t i = 0; i < inputVec.size(); ++i) {
            expect(eq(out[i], inputVec[i])) << "double swap should be identity";
        }
    };
};

int main() {}
