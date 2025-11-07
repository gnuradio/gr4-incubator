#include <boost/ut.hpp>
#include <math.h>
#include <cmath>

#include <gnuradio-4.0/analog/QuadratureDemod.hpp>
using namespace gr::analog;
using namespace boost::ut;

const suite QuadratureDemodTests = [] {
    "Simple Test"_test = [] {
        auto blk = QuadratureDemod<float>();
        blk.gain = 1.0;

        std::vector<std::complex<float>> inputs{{1.0,1.0},{-1.0,1.0}};
        auto _ = blk.processOne(inputs[0]);
        float expected = -M_PI / 2.0;
        double tol = 1e-8;
        auto val = blk.processOne(inputs[1]);
        expect(std::fabs(val - expected) < tol); 
    };

};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }