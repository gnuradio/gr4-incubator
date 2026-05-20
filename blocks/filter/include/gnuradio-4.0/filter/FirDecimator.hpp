#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <execution>
#include <format>
#include <numeric>
#include <numbers>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/algorithm/filter/FilterTool.hpp>

namespace gr::incubator::filter {

namespace detail {

template<typename T>
struct fir_decimator_coeff_type {
    using type = T;
};

template<typename T>
struct fir_decimator_coeff_type<std::complex<T>> {
    using type = T;
};

template<typename T>
using fir_decimator_coeff_type_t = typename fir_decimator_coeff_type<T>::type;

} // namespace detail

GR_REGISTER_BLOCK("gr::incubator::filter::FirDecimator", gr::incubator::filter::FirDecimator, ([T]), [ float, std::complex<float> ])

template<typename T>
requires(std::floating_point<T> || gr::meta::complex_like<T>)
struct FirDecimator : Block<FirDecimator<T>, Resampling<1UZ, 1UZ, false>> {
    using TParent     = Block<FirDecimator<T>, Resampling<1UZ, 1UZ, false>>;
    using Description = Doc<R""(@brief FIR decimator with optional automatic filter design

This block filters and decimates a stream by an integer factor. By default it
designs low-pass FIR taps using GNU Radio 4 filter routines, which is useful
before reducing RF/IQ sample rates. Provide non-empty taps to bypass automatic
tap design.
)"">;
    using CoeffType = detail::fir_decimator_coeff_type_t<T>;

    PortIn<T>  in;
    PortOut<T> out;

    Annotated<uint32_t, "decimation factor", Doc<"Factor by which to downsample after filtering">, Visible> decim{1U};
    Annotated<Tensor<CoeffType>, "taps", Doc<"Optional FIR taps. Empty taps mean design taps from the filter parameters.">, Visible> taps{};

    Annotated<gr::filter::Type, "filter_response", Doc<"Filter response for designed taps">, Visible> filter_response{gr::filter::Type::LOWPASS};
    Annotated<float, "f_low", Doc<"Low cutoff frequency in Hz. For LOWPASS this is the cutoff.">, Visible> f_low{100000.F};
    Annotated<float, "f_high", Doc<"High cutoff frequency in Hz for BANDPASS/BANDSTOP/HIGHPASS">, Visible> f_high{0.F};
    Annotated<float, "sample_rate", Doc<"Input stream sample rate in Hz used for automatic FIR tap design">, Visible> sample_rate{1000000.F};
    Annotated<float, "transition_width", Doc<"Approximate transition width in Hz when num_taps=0">, Visible> transition_width{50000.F};
    Annotated<uint32_t, "num_taps", Doc<"Number of FIR taps. Set 0 or 1 to estimate from transition_width and attenuation_db.">, Visible> num_taps{0U};
    Annotated<float, "gain", Doc<"Designed filter gain">> gain{1.F};
    Annotated<float, "attenuation_db", Doc<"Stop-band attenuation used for automatic order estimation">> attenuation_db{60.F};
    Annotated<float, "beta", Doc<"Kaiser beta used by GNU Radio 4's FIR designer">> beta{6.76F};
    Annotated<gr::algorithm::window::Type, "window", Doc<"Window used for designed taps">> window{gr::algorithm::window::Type::Kaiser};

    GR_MAKE_REFLECTABLE(FirDecimator, in, out, decim, taps, filter_response, f_low, f_high, sample_rate, transition_width, num_taps, gain, attenuation_db, beta, window);

    std::vector<CoeffType> _taps{CoeffType{1}};
    HistoryBuffer<T>       _history{32UZ};
    uint32_t               _decimPhase{0U};
    std::size_t            _debugProcessCalls{0UZ};
    float                  _designSampleRate{1000000.F};

    void start() {
        if (sample_rate > 0.F) {
            _designSampleRate = sample_rate;
        }
        updateFilter();
    }

    void settingsChanged(const property_map& /*oldSettings*/, const property_map& newSettings) {
        if (decim == 0U) {
            throw std::invalid_argument("FirDecimator decim must be greater than zero");
        }

        this->input_chunk_size = static_cast<gr::Size_t>(decim);
        this->output_chunk_size = 1UZ;

        if (newSettings.size() == 1UZ && newSettings.contains("sample_rate")) {
            return;
        }
        if (newSettings.contains("sample_rate") && sample_rate > 0.F) {
            _designSampleRate = sample_rate;
        }
        if (!canUpdateFilter()) {
            return;
        }

        try {
            updateFilter();
        } catch (...) {
            // Settings may be applied in stages. start() performs the final
            // validation once all staged parameters have been committed.
        }
    }

    [[nodiscard]] work::Status processBulk(std::span<const T> input, std::span<T> output) noexcept {
        assert(decim > 0U);
        assert(output.size() >= requiredOutputCount(input.size()));

        std::size_t out_sample_idx = 0UZ;
        const bool  collectDebugStats = debugEnabled();
        float       maxInputMagnitude = 0.F;
        float       maxOutputMagnitude = 0.F;
        bool        sawNonFiniteInput = false;
        bool        sawNonFiniteOutput = false;
        for (const auto& sample : input) {
            if (collectDebugStats) {
                updateDebugSampleStats(sample, maxInputMagnitude, sawNonFiniteInput);
            }
            _history.push_front(sample);
            if (_decimPhase == 0U) {
                auto filtered = filterCurrent();
                if (collectDebugStats) {
                    updateDebugSampleStats(filtered, maxOutputMagnitude, sawNonFiniteOutput);
                }
                output[out_sample_idx++] = filtered;
            }
            _decimPhase = (_decimPhase + 1U) % decim;
        }
        if (collectDebugStats) {
            debugPrintProcessStats(input.size(), out_sample_idx, output.size(), maxInputMagnitude, maxOutputMagnitude, sawNonFiniteInput, sawNonFiniteOutput);
        }
        return work::Status::OK;
    }

    [[nodiscard]] std::size_t requiredOutputCount(std::size_t input_size) const noexcept {
        if (input_size == 0UZ) {
            return 0UZ;
        }
        return (input_size + (static_cast<std::size_t>(decim) - 1UZ - static_cast<std::size_t>(_decimPhase))) / static_cast<std::size_t>(decim);
    }

    [[nodiscard]] T filterCurrent() noexcept {
        if constexpr (gr::meta::complex_like<T>) {
            CoeffType real{};
            CoeffType imag{};
            auto      historyIt = _history.cbegin();
            const auto nTaps = std::min(_taps.size(), _history.size());
            for (std::size_t i = 0UZ; i < nTaps; ++i) {
                const auto tap = _taps[i];
                real += historyIt[i].real() * tap;
                imag += historyIt[i].imag() * tap;
            }
            return T{real, imag};
        } else {
            CoeffType acc{};
            auto      historyIt = _history.cbegin();
            const auto nTaps = std::min(_taps.size(), _history.size());
            for (std::size_t i = 0UZ; i < nTaps; ++i) {
                acc += historyIt[i] * _taps[i];
            }
            return acc;
        }
    }

    void updateFilter() {
        if (decim == 0U) {
            throw std::invalid_argument("FirDecimator decim must be greater than zero");
        }
        this->input_chunk_size = static_cast<gr::Size_t>(decim);
        this->output_chunk_size = 1UZ;

        _taps = taps.value.empty() ? designTaps() : copyConfiguredTaps();
        if (_taps.empty()) {
            throw std::invalid_argument("FirDecimator requires at least one tap");
        }
        if (_taps.size() > _history.capacity()) {
            _history = HistoryBuffer<T>(std::bit_ceil(_taps.size()));
        } else {
            _history.reset();
        }
        _decimPhase = 0U;
        _debugProcessCalls = 0UZ;
        debugPrintTapStats();
    }

    [[nodiscard]] bool canUpdateFilter() const noexcept {
        if (decim == 0U) {
            return false;
        }
        if (!taps.value.empty()) {
            return true;
        }
        return _designSampleRate > 0.F && transition_width > 0.F;
    }

    [[nodiscard]] std::vector<CoeffType> copyConfiguredTaps() const {
        std::vector<CoeffType> copied;
        copied.reserve(taps.value.size());
        std::ranges::copy(taps.value, std::back_inserter(copied));
        return copied;
    }

    [[nodiscard]] std::vector<CoeffType> designTaps() const {
        if (!(_designSampleRate > 0.F)) {
            throw std::invalid_argument("FirDecimator sample_rate must be greater than zero");
        }

        auto designed = designFilterWithTapCount(effectiveNumTaps());
        return std::move(designed.b);
    }

    [[nodiscard]] std::size_t effectiveNumTaps() const {
        if (num_taps > 1U) {
            return static_cast<std::size_t>(num_taps);
        }
        if (!(transition_width > 0.F)) {
            throw std::invalid_argument("FirDecimator transition_width must be greater than zero when num_taps requests automatic tap estimation");
        }
        const double normalised_transition = static_cast<double>(transition_width) / static_cast<double>(_designSampleRate);
        return std::max<std::size_t>(3UZ, gr::filter::fir::estimateNumberOfTapsKaiser(static_cast<double>(attenuation_db), 2.0 * std::numbers::pi * normalised_transition));
    }

    [[nodiscard]] gr::filter::FilterCoefficients<CoeffType> designFilterWithTapCount(std::size_t tap_count) const {
        if (tap_count < 2UZ) {
            throw std::invalid_argument("FirDecimator num_taps must resolve to at least two taps");
        }
        if (tap_count % 2UZ == 0UZ) {
            ++tap_count;
        }

        const auto fs        = static_cast<CoeffType>(_designSampleRate);
        const auto low       = static_cast<CoeffType>(f_low);
        const auto high      = static_cast<CoeffType>(f_high);
        const auto targetGain = static_cast<CoeffType>(gain);
        const auto kaiserBeta = static_cast<CoeffType>(beta);

        switch (filter_response) {
        case gr::filter::Type::LOWPASS: {
            auto coeffs = gr::filter::fir::generateCoefficients<CoeffType>(tap_count, window, low / fs, kaiserBeta);
            normaliseOrThrow(coeffs, CoeffType{0}, targetGain);
            return coeffs;
        }
        case gr::filter::Type::HIGHPASS: {
            auto coeffs = gr::filter::fir::generateCoefficients<CoeffType>(tap_count, window, CoeffType{0.5} - high / fs, kaiserBeta);
            for (std::size_t n = 0UZ; n < tap_count; ++n) {
                coeffs.b[n] *= (n % 2UZ == 0UZ ? CoeffType{1} : CoeffType{-1});
            }
            normaliseOrThrow(coeffs, CoeffType{0.48}, targetGain);
            return coeffs;
        }
        case gr::filter::Type::BANDPASS: {
            auto coeffs     = gr::filter::fir::generateCoefficients<CoeffType>(tap_count, window, low / fs, kaiserBeta);
            auto highPass   = gr::filter::fir::generateCoefficients<CoeffType>(tap_count, window, high / fs, kaiserBeta);
            std::ranges::transform(coeffs.b, highPass.b, coeffs.b.begin(), std::minus<>{});
            const auto centre = static_cast<CoeffType>(std::sqrt(static_cast<double>(f_high) * static_cast<double>(f_low)) / static_cast<double>(_designSampleRate));
            normaliseOrThrow(coeffs, centre, targetGain);
            return coeffs;
        }
        case gr::filter::Type::BANDSTOP: {
            auto coeffs   = gr::filter::fir::generateCoefficients<CoeffType>(tap_count, window, low / fs, kaiserBeta);
            auto highPass = gr::filter::fir::generateCoefficients<CoeffType>(tap_count, window, high / fs, kaiserBeta);
            for (std::size_t n = 0UZ; n < tap_count; ++n) {
                coeffs.b[n] -= highPass.b[n];
                if (n == (tap_count - 1UZ) / 2UZ) {
                    coeffs.b[n] = CoeffType{1} - coeffs.b[n];
                }
            }
            normaliseOrThrow(coeffs, CoeffType{0}, targetGain);
            return coeffs;
        }
        }
        throw std::runtime_error("unexpected FirDecimator filter response");
    }

    static void normaliseOrThrow(gr::filter::FilterCoefficients<CoeffType>& coeffs, CoeffType normalisedFrequency, CoeffType targetGain) {
        const auto [ok, magnitude] = gr::filter::normaliseFilterCoefficients(coeffs, normalisedFrequency, targetGain);
        if (!ok) {
            throw std::invalid_argument(std::format("FirDecimator gain correction failed at normalised frequency {} with magnitude {}", normalisedFrequency, magnitude));
        }
    }

    [[nodiscard]] static bool debugEnabled() noexcept {
        const char* value = std::getenv("GR4_FIR_DECIMATOR_DEBUG");
        return value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
    }

    static void updateDebugSampleStats(const T& sample, float& maxMagnitude, bool& sawNonFinite) noexcept {
        if constexpr (gr::meta::complex_like<T>) {
            sawNonFinite = sawNonFinite || !std::isfinite(sample.real()) || !std::isfinite(sample.imag());
        } else {
            sawNonFinite = sawNonFinite || !std::isfinite(sample);
        }
        const float magnitude = static_cast<float>(std::abs(sample));
        if (std::isfinite(magnitude)) {
            maxMagnitude = std::max(maxMagnitude, magnitude);
        }
    }

    void debugPrintTapStats() const {
        if (!debugEnabled()) {
            return;
        }

        const auto sum = std::accumulate(_taps.cbegin(), _taps.cend(), CoeffType{0});
        const auto [minIt, maxIt] = std::ranges::minmax_element(_taps);
        const bool allFinite = std::ranges::all_of(_taps, [](CoeffType tap) { return std::isfinite(tap); });
        std::println(stderr,
            "[FirDecimator] decim={} chunk={}->{} sample_rate={} active_design_sample_rate={} response={} f_low={} f_high={} transition_width={} attenuation_db={} num_taps={} tap_count={} tap_sum={} tap_min={} tap_max={} taps_finite={}",
            decim.value,
            this->input_chunk_size.value,
            this->output_chunk_size.value,
            sample_rate.value,
            _designSampleRate,
            static_cast<int>(filter_response.value),
            f_low.value,
            f_high.value,
            transition_width.value,
            attenuation_db.value,
            num_taps.value,
            _taps.size(),
            sum,
            minIt == _taps.end() ? CoeffType{0} : *minIt,
            maxIt == _taps.end() ? CoeffType{0} : *maxIt,
            allFinite);
    }

    void debugPrintProcessStats(std::size_t inputSize, std::size_t producedSize, std::size_t outputSize, float maxInputMagnitude, float maxOutputMagnitude, bool sawNonFiniteInput, bool sawNonFiniteOutput) {
        if (!debugEnabled()) {
            return;
        }

        ++_debugProcessCalls;
        if (_debugProcessCalls > 5UZ && _debugProcessCalls % 1000UZ != 0UZ) {
            return;
        }

        std::println(stderr,
            "[FirDecimator] call={} input={} output_capacity={} produced={} decim_phase={} max_in={} max_out={} nonfinite_in={} nonfinite_out={}",
            _debugProcessCalls,
            inputSize,
            outputSize,
            producedSize,
            _decimPhase,
            maxInputMagnitude,
            maxOutputMagnitude,
            sawNonFiniteInput,
            sawNonFiniteOutput);
    }
};

} // namespace gr::incubator::filter
