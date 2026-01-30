/*
 * Copyright 2009,2010,2012 Free Software Foundation, Inc.
 * Copyright 2026 GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <gnuradio-4.0/pfb/PfbArbResamplerKernel.hpp>
#include <gnuradio-4.0/pfb/PfbArbResamplerTaps.hpp>

namespace gr::pfb {

GR_REGISTER_BLOCK("PfbArbResampler", gr::pfb::PfbArbResampler, ([T]), [ float, std::complex<float> ])

template<typename T, typename TAPS_T = T>
struct PfbArbResampler : Block<PfbArbResampler<T, TAPS_T>, gr::Resampling<>, gr::Stride<>> {
    using Base = Block<PfbArbResampler<T, TAPS_T>, gr::Resampling<>, gr::Stride<>>;
    using Base::Base;

    using Description = Doc<"@brief Polyphase filterbank arbitrary resampler (GR3-compatible).">;

    PortIn<T> in;
    PortOut<T> out;

    double rate{1.0};
    std::vector<TAPS_T> taps;
    std::size_t num_filters{32};
    double stop_band_attenuation{100.0};
    std::size_t sample_delay{0};

    GR_MAKE_REFLECTABLE(PfbArbResampler, in, out, rate, taps, num_filters, stop_band_attenuation, sample_delay);

    void settingsChanged(const property_map& /*old_settings*/, const property_map& new_settings) noexcept {
        const bool rate_changed = new_settings.contains("rate");
        const bool taps_changed = new_settings.contains("taps") || new_settings.contains("num_filters") ||
                                  new_settings.contains("stop_band_attenuation");

        if (rate <= 0.0) {
            rate = 1.0;
        }
        if (num_filters == 0) {
            num_filters = 1;
        }

        if (taps.empty()) {
            taps = create_taps<TAPS_T>(rate, num_filters, stop_band_attenuation);
        }

        _kernel.set_num_filters(num_filters);

        if (rate_changed) {
            _kernel.set_rate(rate);
        }

        if (taps_changed || new_settings.contains("taps")) {
            _kernel.set_taps(taps);
        }

        _taps_per_filter = _kernel.taps_per_filter();
        sample_delay = static_cast<std::size_t>(std::max(0, _kernel.group_delay()));

        _choose_chunk_sizes();
        _resize_history_buffer();
    }

    template<class InputSpanLike, class OutputSpanLike>
    gr::work::Status processBulk(InputSpanLike& inSamples, OutputSpanLike& outSamples) {
        static std::atomic<std::size_t> call_id{0};
        const bool debug = std::getenv("GR4_PFB_DEBUG") != nullptr;
        const std::size_t call = debug ? ++call_id : 0;

        const std::size_t nin = inSamples.size();
        const std::size_t nout = outSamples.size();

        if (debug) {
            std::fprintf(stderr,
                         "[PfbArbResampler] call=%zu nin=%zu nout=%zu hist=%zu taps_per_filter=%zu rate=%.6f\n",
                         call, nin, nout, _historyBuffer.size(), _taps_per_filter, rate);
        }

        if constexpr (requires { _historyBuffer.resize(std::size_t{}); }) {
            const std::size_t need = (_taps_per_filter > 0 ? _taps_per_filter - 1 : 0) + nin;
            const std::size_t target = need + 128;
            if (target > _historyBuffer.size()) {
                _historyBuffer.resize(target);
            }
        }

        for (std::size_t i = 0; i < nin; ++i) {
            _historyBuffer.push_back(inSamples[i]);
        }

        int produced = 0;
        int consumed = 0;

        if (_taps_per_filter > 0 && _historyBuffer.size() >= _taps_per_filter && nout > 0) {
            const std::size_t available = _historyBuffer.size() - _taps_per_filter + 1;
            const int n_to_read = static_cast<int>(std::min<std::size_t>(available, static_cast<std::size_t>(std::numeric_limits<int>::max())));
            produced = _kernel.filter(_historyBuffer, n_to_read, &outSamples[0], static_cast<int>(nout), consumed);
            for (int i = 0; i < consumed; ++i) {
                _historyBuffer.pop_front();
            }
        }

        // if (debug) {
        //     std::fprintf(stderr,
        //                  "[PfbArbResampler] call=%zu produced=%d consumed=%d hist_after=%zu\n",
        //                  call, produced, consumed, _historyBuffer.size());
        //     if (produced == 0 && consumed == 0 && nin > 0) {
        //         std::fprintf(stderr,
        //                      "[PfbArbResampler] call=%zu stalled (no produce/consume) nin=%zu nout=%zu\n",
        //                      call, nin, nout);
        //     }
        // }

        std::ignore = inSamples.consume(nin);
        outSamples.publish(static_cast<std::size_t>(produced));
        if (this->inputTagsPresent()) {
            const auto& tag = this->mergedInputTag();
            const auto  it  = tag.map.find(gr::tag::SAMPLE_RATE.shortKey());
            if (it != tag.map.end()) {
                if (const auto* v = std::get_if<float>(&it->second)) {
                    const float new_rate = static_cast<float>((*v) * rate);
                    this->publishTag({{gr::tag::SAMPLE_RATE.shortKey(), new_rate}}, 0UZ);
                }
            }
        }
        return gr::work::Status::OK;
    }

private:
    kernel::PfbArbResamplerKernel<T, TAPS_T> _kernel{};
    std::size_t _taps_per_filter{0};
    HistoryBuffer<T> _historyBuffer{1024};

    void _choose_chunk_sizes() {
        constexpr std::size_t base = 1024;
        std::size_t in_chunk = base;
        std::size_t out_chunk = static_cast<std::size_t>(std::max(1.0, std::floor(static_cast<double>(in_chunk) * std::max(rate, 1e-9))));
        if (rate < 1.0) {
            out_chunk = base;
            in_chunk = static_cast<std::size_t>(std::max(1.0, std::floor(static_cast<double>(out_chunk) / std::max(rate, 1e-9))));
        }
        this->input_chunk_size = in_chunk;
        this->output_chunk_size = out_chunk;
    }

    void _resize_history_buffer() {
        const std::size_t guard = 128;
        const std::size_t cap = _taps_per_filter + std::max<std::size_t>(this->input_chunk_size, 1) + guard;
        if constexpr (requires { _historyBuffer.resize(std::size_t{}); }) {
            _historyBuffer.resize(cap);
        }

        while (_historyBuffer.size() < (_taps_per_filter > 0 ? _taps_per_filter - 1 : 0)) {
            _historyBuffer.push_back(T{});
        }
    }
};

} // namespace gr::pfb
