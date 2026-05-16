// qa_DCOffsetChannel.cpp — per-block functional tests
#include <gnuradio-4.0/channel/DCOffsetChannel.hpp>
#include <boost/ut.hpp>
#include <complex>
#include <format>
#include <vector>

using namespace boost::ut;

const boost::ut::suite<"DCOffsetChannel"> dcOffTests = [] {
    using namespace boost::ut;

    "shifts mean by (dc_i, dc_q)"_test = [] {
        gr::incubator::channel::DCOffsetChannel<float> ch;
        ch.dc_i = 0.3f;
        ch.dc_q = -0.2f;
        float sumI = 0.f, sumQ = 0.f;
        for (int i = 0; i < 1000; ++i) {
            const auto y = ch.processOne({0.f, 0.f});
            sumI += y.real(); sumQ += y.imag();
        }
        expect(approx(sumI / 1000.f, 0.3f, 1e-4f));
        expect(approx(sumQ / 1000.f, -0.2f, 1e-4f));
    };

    "zero offset is identity"_test = [] {
        gr::incubator::channel::DCOffsetChannel<float> ch;
        const std::complex<float> s{1.f, -1.f};
        expect(eq(ch.processOne(s), s));
    };
};

const boost::ut::suite<"DCOffsetChannel extended"> dcOffsetExtTests = [] {
    "zero offsets is identity"_test = [] {
        gr::incubator::channel::DCOffsetChannel<float> ch;
        ch.dc_i = 0.f;
        ch.dc_q = 0.f;
        for (auto& in : std::vector<std::complex<float>>{{1.f,0.f},{-1.f,0.5f},{0.f,1.f}}) {
            expect(eq(ch.processOne(in), in)) << "zero DC offset must be identity";
        }
    };

    "known DC shifts real part"_test = [] {
        gr::incubator::channel::DCOffsetChannel<float> ch;
        ch.dc_i = 0.5f;
        ch.dc_q = 0.f;
        const auto y = ch.processOne({1.f, 0.f});
        expect(approx(y.real(), 1.5f, 1e-5f));
        expect(approx(y.imag(), 0.f,  1e-5f));
    };

    "output mean equals DC value for zero-mean input"_test = [] {
        gr::incubator::channel::DCOffsetChannel<float> ch;
        ch.dc_i = 0.3f;
        ch.dc_q = 0.7f;

        // zero-mean input: +1,-1,+1,-1,...
        float sumI = 0.f, sumQ = 0.f;
        constexpr int N = 100;
        for (int i = 0; i < N; ++i) {
            const float sign = (i % 2 == 0) ? 1.f : -1.f;
            const auto y = ch.processOne({sign, -sign});
            sumI += y.real();
            sumQ += y.imag();
        }
        expect(approx(sumI / N, ch.dc_i, 0.01f))
            << std::format("I mean={:.4f} expected dc_i={:.4f}", sumI / N, static_cast<float>(ch.dc_i));
        expect(approx(sumQ / N, ch.dc_q, 0.01f))
            << std::format("Q mean={:.4f} expected dc_q={:.4f}", sumQ / N, static_cast<float>(ch.dc_q));
    };
};

int main() {}
