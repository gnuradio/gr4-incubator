#include <boost/ut.hpp>

#include <gnuradio-4.0/basic/Copy.hpp>
using namespace gr::basic;
using namespace boost::ut;

const suite CopyTests = [] {
    "Simple Test"_test = [] {
        Copy<float> blk;

        // Basic bitwise AND operation

        float value = 483732.9227;
        expect(eq(blk.processOne(value), value)); // 0xFF & 0x0F = 0x0F
    };

};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }