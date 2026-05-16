#pragma once

#include <gnuradio-4.0/Block.hpp>

#include <complex>
#include <cstdint>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <vector>

namespace gr::incubator::basic {
using namespace gr;

template<typename T>
struct MovingAverage : Block<MovingAverage<T>> {
    using Description = Doc<
        "Rectangular-window moving average: y[n] = (1/N)*sum(x[n-k], k=0..N-1). "
        "Uses an efficient circular buffer and running sum for O(1) per sample. "
        "Initialised to zero — output ramps up during the first window_size samples. "
        "Use cases: power/energy smoothing upstream of AGC or SNR estimation; "
        "simple low-pass filtering where exact tap control is not needed; "
        "windowed averaging for any scalar or complex signal. "
        "Signal chain: [any source] -> MovingAverage -> [AGC / SNREstimator / threshold].">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<uint32_t, "window_size", Visible, Doc<"Averaging window length N (samples)">> window_size = 8u;

    GR_MAKE_REFLECTABLE(MovingAverage, in, out, window_size);

    std::vector<std::complex<T>> _buf;
    std::complex<T>              _sum{T(0), T(0)};
    std::size_t                  _head{0u};

    void start() noexcept { _rebuild(); }
    void settingsChanged(const property_map&, const property_map&) noexcept { _rebuild(); }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> x) noexcept {
        const std::size_t N = _buf.size();
        if (N == 0u) {
            return x;
        }

        _sum -= _buf[_head];
        _buf[_head] = x;
        _sum += x;
        _head = (_head + 1u) % N;
        return _sum / static_cast<T>(N);
    }

    void _rebuild() noexcept {
        const std::size_t N = static_cast<std::size_t>(static_cast<uint32_t>(window_size));
        _buf.assign(N, std::complex<T>{T(0), T(0)});
        _sum  = std::complex<T>{T(0), T(0)};
        _head = 0u;
    }
};

GR_REGISTER_BLOCK("gr::incubator::basic::MovingAverage", gr::incubator::basic::MovingAverage, ([T]), [ uint8_t, int16_t, int32_t, float, double, std::complex<float>, std::complex<double> ])

} // namespace gr::incubator::basic
