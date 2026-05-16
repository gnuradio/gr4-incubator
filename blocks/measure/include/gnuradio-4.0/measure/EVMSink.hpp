#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <cmath>
#include <complex>
#include <span>

namespace gr::incubator::measure {
using namespace gr;

template<typename T>
struct EVMSink : Block<EVMSink<T>> {
    using Description = Doc<
        "Error Vector Magnitude (EVM) sink, dual-port version. "
        "Accepts two input streams: in=received symbols r[n], reference=ideal reference symbols c[n]. "
        "Accumulates and computes: RMS EVM% = 100*sqrt(sum|r[n]-c[n]|^2 / sum|c[n]|^2), "
        "peak EVM = 100*max(|r[n]-c[n]|) / sqrt(mean|c|^2). "
        "Accessors: evm_rms() (RMS EVM in percent), evm_peak() (peak EVM in percent), n_symbols() (count). "
        "Call start() to reset accumulators; results are updated after each processBulk call. "
        "Complement to BERSink: BERSink counts hard-decision errors; EVMSink measures analog impairments "
        "(phase noise, IQ imbalance, timing jitter) visible in the constellation before hard decisions.">;

    PortIn<std::complex<T>> in;
    PortIn<std::complex<T>> reference;

    Annotated<std::string, "constellation", Visible, Doc<"Constellation name for display ('bpsk','qpsk','16qam')">> constellation = std::string{"bpsk"};

    Annotated<bool, "normalize", Visible, Doc<"Normalise EVM by mean reference power (true) or per-symbol (false)">> normalize = true;

    GR_MAKE_REFLECTABLE(EVMSink, in, reference, constellation, normalize);

    T        _sumEVM2{T(0)};
    T        _sumRef2{T(0)};
    T        _peakErr{T(0)};
    uint64_t _count{0u};
    T        _rmsResult{T(0)};
    T        _peakResult{T(0)};

    void start() noexcept { _reset(); }
    void settingsChanged(const property_map& /*old*/, const property_map& /*new*/) noexcept { _reset(); }

    [[nodiscard]] T        evm_rms() const noexcept { return _rmsResult; }
    [[nodiscard]] T        evm_peak() const noexcept { return _peakResult; }
    [[nodiscard]] uint64_t n_symbols() const noexcept { return _count; }

    [[nodiscard]] work::Status processBulk(std::span<const std::complex<T>> recvSpan, std::span<const std::complex<T>> refSpan) noexcept {

        const std::size_t n = std::min(recvSpan.size(), refSpan.size());
        for (std::size_t i = 0u; i < n; ++i) {
            const T errMag = std::abs(recvSpan[i] - refSpan[i]);
            _sumEVM2 += errMag * errMag;
            _sumRef2 += std::norm(refSpan[i]);
            if (errMag > _peakErr) {
                _peakErr = errMag;
            }
        }
        _count += n;

        if (_count > 0u && _sumRef2 > T(1e-20)) {
            _rmsResult     = T(100) * std::sqrt(_sumEVM2 / _sumRef2);
            const T rmsRef = std::sqrt(_sumRef2 / static_cast<T>(_count));
            _peakResult    = (rmsRef > T(1e-20)) ? (T(100) * _peakErr / rmsRef) : T(0);
        }
        return work::Status::OK;
    }

private:
    void _reset() noexcept {
        _sumEVM2    = T(0);
        _sumRef2    = T(0);
        _peakErr    = T(0);
        _count      = 0u;
        _rmsResult  = T(0);
        _peakResult = T(0);
    }
};

GR_REGISTER_BLOCK("gr::incubator::measure::EVMSink", gr::incubator::measure::EVMSink, ([T]), [ float, double ])

} // namespace gr::incubator::measure
