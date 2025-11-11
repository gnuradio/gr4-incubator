#include <boost/ut.hpp>

#include <gnuradio-4.0/basic/Converters.hpp>

using namespace gr::basic;
using namespace boost::ut;

const suite StreamToPmtTests = [] {
    "StreamToPmt"_test = [] {
        gr::Graph fg;
        size_t             packet_size = 1234;
        auto& blk = fg.emplaceBlock<StreamToPmt<float>>({
            {"packet_size", packet_size},
        });
        // blk.settings().init();
        // blk.settings().applyStagedParameters();
        // StreamToPmt<float> blk;
        // blk.packet_size = 1234;
        
        std::vector<float>     in(packet_size * 4);
        std::vector<pmtv::pmt> out(4);
        std::ignore = blk.processBulk(in, out);
        expect(eq(out.size(), 4));
        expect(eq(out[0].size(), packet_size));
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }