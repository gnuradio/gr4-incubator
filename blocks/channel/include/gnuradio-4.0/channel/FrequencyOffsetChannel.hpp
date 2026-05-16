#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <cmath>
#include <complex>
#include <numbers>

namespace gr::incubator::channel {
using namespace gr;

template<typename T>
struct FrequencyOffsetChannel : Block<FrequencyOffsetChannel<T>> {
    using Description = Doc<
        "Deterministic frequency offset channel. Multiplies each input sample by a rotating phasor: "
        "phi[n] = phi[n-1] + 2*pi*freq_offset_norm, y[n] = x[n]*exp(j*phi[n]). "
        "Models TX/RX local-oscillator (LO) frequency mismatch in a controlled, deterministic way. "
        "freq_offset_norm is the offset in cycles per sample (f_offset/f_sample); "
        "typical values: +/-0.001 to +/-0.01 (+/-1 to +/-10 kHz at 1 Msps). "
        "Use instead of PhaseNoiseChannel for a clean test signal for carrier-recovery validation. "
        "Signal chain: BPSKModulator -> FrequencyOffsetChannel -> CostasLoop -> BPSKDemodulator -> BERSink.">;

    PortIn<std::complex<T>>  in;
    PortOut<std::complex<T>> out;

    Annotated<T, "freq_offset_norm", Visible, Doc<"Frequency offset normalised to sample rate (f_off / f_s); typical: ±0.001 to ±0.01">> freq_offset_norm = T(0.01);

    GR_MAKE_REFLECTABLE(FrequencyOffsetChannel, in, out, freq_offset_norm);

    T _phase{T(0)};

    void start() { _phase = T(0); }
    void settingsChanged(const property_map&, const property_map&) noexcept { _phase = T(0); }

    [[nodiscard]] std::complex<T> processOne(std::complex<T> x) noexcept {
        constexpr T twoPi = T(2) * static_cast<T>(std::numbers::pi);
        _phase += twoPi * static_cast<T>(freq_offset_norm);
        // Wrap to avoid floating-point drift
        if (_phase > static_cast<T>(std::numbers::pi)) {
            _phase -= twoPi;
        }
        if (_phase < -static_cast<T>(std::numbers::pi)) {
            _phase += twoPi;
        }
        return x * std::polar(T(1), _phase);
    }
};

GR_REGISTER_BLOCK("gr::incubator::channel::FrequencyOffsetChannel", gr::incubator::channel::FrequencyOffsetChannel, ([T]), [ float, double ])

} // namespace gr::incubator::channel
