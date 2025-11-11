#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>

namespace gr::polyphase {

template<typename T, typename TAPS_T = T>
struct PfbArbitraryResampler : Block<PfbArbitraryResampler<T>, gr::Resampling<>, gr::Stride<>> {

    using sample_type = T;
    using taps_type   = TAPS_T;

    using Description = Doc<"@Polyphase Filterbank Arbitrary Resampler (Fred harris PFB, Ch.7.5)">;

    PortIn<T>  in;
    PortOut<T> out;

    // User-configurable properties
    double              rate{1.0};                    // arbitrary rate (>0)
    std::vector<TAPS_T> taps;                         // prototype LPF taps, length multiple of num_filters (will pad)
    size_t              num_filters{32};              // P (number of polyphase filters)
    double              stop_band_attenuation{100.0}; // reserved (tap design outside scope here)
    size_t              sample_delay{0};              // reported/filter group delay (approx)

    // ---- Runtime / internal state ----
    // Polyphase bank: poly_[p] holds taps for phase p, size K_
    std::vector<std::vector<TAPS_T>> poly_;
    size_t                           P_{32}; // == num_filters
    size_t                           K_{0};  // taps per phase

    // harris accumulator
    double phase_{0.0}; // in [0, P_)
    double d_inc_{1.0}; // = P_ / rate

    // Input history buffer (must be kept across calls)
    HistoryBuffer<T> _historyBuffer{1024};

    GR_MAKE_REFLECTABLE(PfbArbitraryResampler, in, out, rate, taps, num_filters, stop_band_attenuation, sample_delay);

    // --- helpers ------------------------------------------------------------

    void _build_poly(const std::vector<TAPS_T>& user_taps, size_t P) {
        P_ = P;
        // Ensure taps length is a multiple of P (pad with zeros)
        const size_t L             = user_taps.size();
        K_                         = (L + P_ - 1) / P_; // ceil
        std::vector<TAPS_T> padded = user_taps;
        padded.resize(P_ * K_, TAPS_T{0});

        poly_.clear();
        poly_.resize(P_);
        for (size_t p = 0; p < P_; ++p) {
            poly_[p].resize(K_);
            // phase p takes h[p], h[p+P], h[p+2P], ...
            for (size_t k = 0; k < K_; ++k) {
                poly_[p][k] = padded[p + k * P_]; // oldest -> newest
            }
        }
    }

    // Approximate group delay in input samples (for info)
    static size_t _estimate_group_delay(size_t K, size_t /*P*/) {
        // For PFB arbitrary resampler, a reasonable user-facing number is ~ (K-1)/2
        return (K > 0) ? (K - 1) / 2 : 0;
    }

    // Choose chunk sizes to roughly match rate (keeps scheduler happy)
    void _choose_chunk_sizes() {
        // GR4 usually lets you set these on the ports or the block (naming varies).
        // We’ll keep output ~= input * rate, but clamp to sane ranges.
        constexpr size_t BASE      = 1024;
        size_t           in_chunk  = BASE;
        size_t           out_chunk = static_cast<size_t>(std::max(1.0, std::floor(static_cast<double>(in_chunk) * std::max(rate, 1e-9))));
        // If rate<1, invert the choice so input chunk scales to get at least 1 output
        if (rate < 1.0) {
            out_chunk = BASE;
            in_chunk  = static_cast<size_t>(std::max(1.0, std::floor(static_cast<double>(out_chunk) / std::max(rate, 1e-9))));
        }

        // If your GR4 has explicit APIs, use them; otherwise these names are common:
        this->input_chunk_size  = in_chunk;
        this->output_chunk_size = out_chunk;
    }

    // --- lifecycle ----------------------------------------------------------


    void _resize_history_buffer() {
        // max input advance per output (harris accumulator)
        const size_t max_adv = static_cast<size_t>(
            std::ceil(static_cast<double>(P_) / std::max(rate, 1e-12)));

        const size_t guard = 128;

        // Start with a sane estimate that does not depend on GR4 property wrappers
        size_t in_chunk_est = std::max<size_t>(1024, 2 * K_);

        // If your block has input_chunk_size as an Annotated integral, read it by casting.
        // Use requires so this compiles even if the member doesn't exist in some builds.
        if constexpr (requires { static_cast<unsigned int>(this->input_chunk_size); }) {
            in_chunk_est = std::max(
                in_chunk_est,
                static_cast<size_t>(static_cast<unsigned int>(this->input_chunk_size)));
        }

        const size_t cap = K_ + std::max(in_chunk_est, max_adv) + guard;

        if constexpr (requires { _historyBuffer.resize(size_t{}); }) {
            _historyBuffer.resize(cap);
        } // else: no-op for auto-expiring buffers

        // Prime K_-1 zeros so the first convolution window is valid.
        while (_historyBuffer.size() < (K_ > 0 ? K_ - 1 : 0)) {
            _historyBuffer.push_back(T{});
        }
    }

    void settingsChanged(const property_map& /*old_settings*/, const property_map& new_settings) noexcept {
        const bool taps_changed = new_settings.contains("taps") || new_settings.contains("num_filters");
        const bool rate_changed = new_settings.contains("rate");

        if (taps.empty()) {
            // Provide a minimal placeholder if user didn’t set taps yet
            taps                        = std::vector<TAPS_T>(num_filters * 16, TAPS_T{0});
            taps[(taps.size() - 1) / 2] = TAPS_T{1};
        }

        if (taps_changed) {
            _build_poly(taps, num_filters);
            sample_delay = (K_ > 0) ? (K_ - 1) / 2 : 0;
        }

        if (rate <= 0.0) {
            rate = 1.0;
        }
        if (rate_changed || taps_changed) {
            P_     = num_filters;
            d_inc_ = static_cast<double>(P_) / rate;
            _choose_chunk_sizes();
            _resize_history_buffer();
        }
    }

    // --- work ---------------------------------------------------------------

    // Assumptions about HistoryBuffer<T>:
    //  - size(): current number of elements retained
    //  - push_back(value) / push_front(value): inserts and expires oldest automatically
    //  - pop_front(): removes oldest element (if any)
    //  - operator[](i): random access with 0 == oldest, size()-1 == newest
    // If your HistoryBuffer has different APIs, adjust the index access below accordingly.

    template<class InputSpanLike, class OutputSpanLike>
    gr::work::Status processBulk(InputSpanLike& inSamples, OutputSpanLike& outSamples) {
        using Sample = std::remove_reference_t<decltype(outSamples[0])>;

        const size_t nin  = inSamples.size();
        const size_t nout = outSamples.size();

        // 1) Append *all* new input samples into history (we will count how many we actually consume).
        //    The scheduler learns how many we consumed via inSamples.consume(nConsumedFromThisCall) at the end.
        const size_t pre_hist_size = _historyBuffer.size();
        for (size_t i = 0; i < nin; ++i) {
            _historyBuffer.push_back(inSamples[i]); // expires oldest as needed
        }

        // Variables to track what we publish/consume this call
        size_t produced_this_call        = 0;
        size_t consumed_from_old_history = 0; // pops that came from pre-call history
        size_t consumed_from_this_call   = 0; // pops that came from the newly appended nin

        size_t old_head_budget = pre_hist_size; // how many oldest elements belong to *previous* calls

        // 2) Emit outputs until (a) out buffer full, or (b) not enough history left for another dot product,
        //    or (c) we would need to consume beyond what's available from this call + old history.
        while (produced_this_call < nout) {
            if (_historyBuffer.size() < K_) {
                break; // need at least K_ samples for one output
            }

            // (a) pick phase
            const size_t  p  = static_cast<size_t>(phase_); // 0..P_-1
            const TAPS_T* ht = poly_[p].data();             // K_ taps for this phase

            // (b) FIR dot over last K_ samples (oldest->newest alignment)
            // Oldest index of the K-window:
            const size_t base = _historyBuffer.size() - K_;
            Sample       acc{};
            for (size_t i = 0; i < K_; ++i) {
                acc += _historyBuffer[base + i] * static_cast<TAPS_T>(ht[i]);
            }

            outSamples[produced_this_call++] = acc;

            // (c) Advance the harris accumulator and pop `adv` input samples
            phase_ += d_inc_;
            const size_t adv = static_cast<size_t>(phase_ / static_cast<double>(P_));
            if (adv) {
                phase_ -= static_cast<double>(adv) * static_cast<double>(P_);
                for (size_t a = 0; a < adv; ++a) {
                    if (_historyBuffer.size() == 0) {
                        break; // safety
                    }
                    // Account whether this pop consumes from prior history or from the samples we just pushed.
                    if (old_head_budget > 0) {
                        --old_head_budget;
                        ++consumed_from_old_history;
                    } else if (consumed_from_this_call < nin) {
                        ++consumed_from_this_call;
                    }
                    _historyBuffer.pop_front(); // discard oldest
                }
            }

            // (d) If we *must* advance more than what we've appended + old history, we must stop.
            // (In practice, the pop loop + size() guard already enforces this.)
        }

        // 3) Report to scheduler
        // Never claim to consume more than provided in this call.
        if (consumed_from_this_call > nin) {
            consumed_from_this_call = nin;
        }

        std::ignore = inSamples.consume(consumed_from_this_call);
        outSamples.publish(produced_this_call);

        return gr::work::Status::OK;
    }
};

} // namespace gr::polyphase

GR_REGISTER_BLOCK("Polyphase Arbitrary Resmapler Block", gr::polyphase::PfbArbitraryResampler, ([T]), [ float, std::complex<float> ])