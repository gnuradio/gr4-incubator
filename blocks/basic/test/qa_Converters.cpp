#include <boost/ut.hpp>

#include <gnuradio-4.0/basic/Converters.hpp>

using namespace gr::basic;
using namespace boost::ut;

const suite StreamToPmtTests = [] {
    "StreamToPmt"_test = [] {
        size_t packet_size = 1234;
        StreamToPmt<float> blk({
        {"packet_size", packet_size},});
        
        std::vector<float> in(packet_size*4);
        std::vector<pmtv::pmt> out(4);
        blk.processBulk(in, out);
        expect(eq(out.size(), 4)); 
        expect(eq(out[0].size(), packet_size)); 
    };

};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }