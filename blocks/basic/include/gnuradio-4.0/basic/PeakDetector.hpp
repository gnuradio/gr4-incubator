#pragma once

#include <cmath>
#include <cstdint>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::incubator::basic {

using namespace gr;

template<typename T>
struct PeakDetector : Block<PeakDetector<T>> {
    using Description = Doc<"Detects local maxima in a real-valued stream. Outputs 1 when the centre sample of a "
                            "3-sample window exceeds threshold AND is strictly greater than both its neighbours; otherwise 0. "
                            "min_gap enforces a minimum number of samples between successive peak outputs, preventing "
                            "a broad peak from triggering multiple times due to noise on a plateau. "
                            "Output is delayed by one sample relative to the input (lookahead requirement). "
                            "Typical uses: carrier frequency peak in a periodogram, pilot tone finder, "
                            "burst/preamble power peak detection.">;

    PortIn<T>        in;
    PortOut<uint8_t> out;

    Annotated<T, "threshold", Visible, Doc<"Minimum sample value for a peak candidate">> threshold = T(0.5);

    Annotated<gr::Size_t, "min_gap", Visible, Doc<"Minimum samples between successive peak outputs (0 = disabled)">> min_gap = gr::Size_t{0u};

    GR_MAKE_REFLECTABLE(PeakDetector, in, out, threshold, min_gap);

    T           _prev{T(0)};
    T           _cur{T(0)};
    std::size_t _gapCount{0u};

    void start() noexcept {
        _prev     = T(0);
        _cur      = T(0);
        _gapCount = 0u;
    }

    void settingsChanged(const property_map& /*old*/, const property_map& /*new*/) noexcept { start(); }

    [[nodiscard]] uint8_t processOne(T next) noexcept {
        // Shift window: [_prev, _cur, next]
        // We test whether _cur is the peak
        const T    thr = static_cast<T>(threshold);
        const auto gap = static_cast<std::size_t>(static_cast<gr::Size_t>(min_gap));

        uint8_t result = 0u;
        if (_gapCount > 0u) {
            --_gapCount;
        }

        if (_cur > thr && _cur > _prev && _cur > next && _gapCount == 0u) {
            result    = 1u;
            _gapCount = gap;
        }

        _prev = _cur;
        _cur  = next;
        return result;
    }
};

GR_REGISTER_BLOCK("gr::incubator::basic::PeakDetector", gr::incubator::basic::PeakDetector, ([T]), [ float, double ])

} // namespace gr::incubator::basic
