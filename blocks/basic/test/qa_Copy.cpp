#include <boost/ut.hpp>

#include <gnuradio-4.0/basic/Copy.hpp>
using namespace gr::basic;
using namespace boost::ut;

const suite CopyTests = [] {
    "Simple Test"_test = [] {
        Copy<float> blk;

        float value = 483732.9227;
        expect(eq(blk.processOne(value), value)); 
    };

};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }