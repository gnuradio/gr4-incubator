#pragma once

#include <complex>
#include <cstdint>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <span>
#include <type_traits>

namespace gr::incubator::basic {
using namespace gr;

namespace detail {
// For real T: use value directly; for complex<T>: use magnitude.
template<typename T>
T peakMagnitude(T x) noexcept {
    return x;
}
template<typename T>
T peakMagnitude(std::complex<T> x) noexcept {
    return std::abs(x);
}
} // namespace detail

template<typename T>
struct PeakTagger : Block<PeakTagger<T>> {
    using Description = Doc<
        "1:1 passthrough with 1-sample delay that writes peak tags on local magnitude maxima. "
        "When magnitude(x[n-1]) > magnitude(x[n-2]) AND magnitude(x[n-1]) > magnitude(x[n]), "
        "a tag {\"peak\": true, \"peak_value\": magnitude(x[n-1])} is published on that output sample. "
        "Works for both real T (only positive peaks detected) and complex<T> (envelope peaks). "
        "The very first output sample is T{} due to the lookahead delay. "
        "Signal chain: [signal] -> PeakTagger -> [downstream] + [tag consumer for peak locations].">;

    PortIn<T>  in;
    PortOut<T> out;

    GR_MAKE_REFLECTABLE(PeakTagger, in, out);

    using Scalar = std::conditional_t<std::is_floating_point_v<T>, T, std::conditional_t<std::is_integral_v<T>, float, float>>;

    T _delay1{}; // most recent input, not yet output
    T _delay2{}; // one older than _delay1

    void start() noexcept {
        _delay1 = T{};
        _delay2 = T{};
    }

    [[nodiscard]] T processOne(T x) noexcept {
        const auto m2 = detail::peakMagnitude(_delay2);
        const auto m1 = detail::peakMagnitude(_delay1);
        const auto m0 = detail::peakMagnitude(x);

        T out_sample = _delay1;

        // _delay1 is a strict local maximum if m2 < m1 > m0
        if (m1 > m2 && m1 > m0) {
            property_map tm;
            tm[std::pmr::string("peak")]       = true;
            tm[std::pmr::string("peak_value")] = static_cast<double>(m1);
            this->publishTag(tm, 0UZ);
        }

        _delay2 = _delay1;
        _delay1 = x;
        return out_sample;
    }
};

GR_REGISTER_BLOCK("gr::incubator::basic::PeakTagger", gr::incubator::basic::PeakTagger, ([T]), [ uint8_t, int16_t, int32_t, float, double, std::complex<float>, std::complex<double> ])

} // namespace gr::incubator::basic
