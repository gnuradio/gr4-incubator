// RtAudioSink.hpp
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <format>
#include <algorithm>

#include <RtAudio.h>


#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>


namespace gr::audio {

template <typename T> struct is_std_pair : std::false_type {};
template <typename A, typename B> struct is_std_pair<std::pair<A,B>> : std::true_type {};
template <typename T> inline constexpr bool is_std_pair_v = is_std_pair<T>::value;

// Detect “map-like” (has key_type/mapped_type and find())
template <typename T, typename = void>
struct is_map_like : std::false_type {};
template <typename T>
struct is_map_like<T, std::void_t<
    typename T::key_type,
    typename T::mapped_type,
    decltype(std::declval<const T&>().find(std::declval<const typename T::key_type&>()))
>> : std::true_type {};
template <typename T> inline constexpr bool is_map_like_v = is_map_like<T>::value;



template<typename T>
struct RtAudioSink : Block<RtAudioSink<T>> {
    static_assert(std::is_same_v<T, float>, "RtAudioSink expects T=float (float32 interleaved).");

    using Description = Doc<
    "@brief Plays interleaved float32 audio via RtAudio. "
    "Discovers 'num_channels' and 'sample_rate' from input tags; "
    "falls back to attributes. "
    "Reopens the audio stream if those values change."
    >;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    PortIn<T> in;

    // ---- Attributes (fallbacks / defaults) ----
    A<uint32_t, "sample_rate",     Doc<"Fallback sample rate (Hz) if not tagged)">, Visible>         sample_rate   = 48000;
    A<uint32_t, "channels",        Doc<"Fallback channel count if not tagged (0 = wait for tag)">, Visible> channels_fallback = 0;
    A<uint32_t, "frames_per_buf",  Doc<"RtAudio buffer size (frames)">, Visible>                      frames_per_buf = 256;
    A<int32_t,  "device_index",    Doc<"RtAudio output device index (-1 = default)">, Visible>        device_index   = -1;
    A<bool,     "dither",          Doc<"Enable RtAudio dither (if backend supports it)">>             dither         = false;
    A<double,   "target_latency_s",Doc<"Target FIFO latency seconds">, Visible>                        target_latency_s = 0.100;

    GR_MAKE_REFLECTABLE(RtAudioSink, in, sample_rate, channels_fallback, frames_per_buf, device_index, dither, target_latency_s);

    // ---- Tag keys (edit if your tag names differ) ----
    static constexpr const char* kTagNumChannels = "num_channels";
    static constexpr const char* kTagSampleRate  = "sample_rate";

    // ===== Internals =====
    struct FIFO {
        std::vector<float> buf;
        size_t r = 0, w = 0, n = 0;
        std::mutex m;
        void reset(size_t cap) {
            std::scoped_lock lk(m);
            buf.assign(cap, 0.0f); r = w = n = 0;
        }
        size_t capacity() const { return buf.size(); }
        size_t push(const float* data, size_t c) {
            std::scoped_lock lk(m);
            size_t free = buf.size() - n, k = std::min(free, c);
            for (size_t i = 0; i < k; ++i) { buf[w] = data[i]; w = (w + 1) % buf.size(); }
            n += k; return k;
        }
        size_t pop(float* out, size_t c) {
            std::scoped_lock lk(m);
            size_t k = std::min(n, c);
            for (size_t i = 0; i < k; ++i) { out[i] = buf[r]; r = (r + 1) % buf.size(); }
            n -= k; return k;
        }
    };

    RtAudio _dac;
    bool    _stream_open    = false;
    bool    _stream_running = false;

    // “Resolved” runtime format (what the RtAudio stream currently uses)
    uint32_t _channels = 0;
    uint32_t _sr       = 0;

    // “Pending” format discovered from tags / fallbacks
    uint32_t _pending_channels = 0;
    uint32_t _pending_sr       = 0;

    FIFO    _fifo;
    std::atomic<bool> _underflow{false};

    void start() {
        _stream_open = _stream_running = false;
        _channels = _sr = 0;
        _pending_channels = channels_fallback.value;     // fallback if tag doesn’t arrive
        _pending_sr       = sample_rate.value;           // fallback if tag doesn’t arrive
        // Don’t open yet; wait to see if tags provide a better format immediately.
    }

    void stop() { close_stream(); }

    // ===== RtAudio callback =====
    static int rtaudio_cb(void* outputBuffer, void*, unsigned int frames,
                          double, RtAudioStreamStatus, void* userData)
    {
        auto* self = static_cast<RtAudioSink*>(userData);
        float* out = static_cast<float*>(outputBuffer);
        const size_t need = static_cast<size_t>(frames) * self->_channels;
        size_t got = self->_fifo.pop(out, need);
        if (got < need) {
            std::fill(out + got, out + need, 0.0f);
            self->_underflow.store(true, std::memory_order_relaxed);
        } else {
            self->_underflow.store(false, std::memory_order_relaxed);
        }
        return 0;
    }

    // ===== Work loop =====
    [[nodiscard]] constexpr work::Status processBulk(InputSpanLike auto& dataIn) noexcept {
        // 1) Scan tags to discover num_channels and sample_rate (if present)
        scan_format_tags_(dataIn);

        // 2) Open/reopen stream if format not open yet or changed
        if ((!_stream_open && ready_to_open_()) ||
            (_stream_open && format_changed_())) {
            if (!open_or_reopen_stream_()) return work::Status::DONE;
        }

        // 3) If still not open (e.g., waiting for num_channels), just consume and drop this buffer
        if (!_stream_open) {
            (void)dataIn.consume(dataIn.size());
            return work::Status::OK;
        }

        // 4) Push audio
        const size_t n = dataIn.size();
        if (n) {
            const float* src = reinterpret_cast<const float*>(dataIn.data());
            size_t pushed = _fifo.push(src, n);
            (void)dataIn.consume(pushed);
        }
        return work::Status::OK;
    }

private:
    // --- Tag scanning
    template <typename SpanT>
    void scan_format_tags_(SpanT& dataIn) {
        for (const auto& t : dataIn.tags()) {
            try_extract_(t);  
        }
    }

    template <typename TagT>
    void try_extract_(const TagT& tag) {
        const auto* props = get_props_(tag);   // always returns const property_map* or nullptr
        if (!props) return;

        if (auto v = get_uint_(*props, kTagNumChannels)) {
            _pending_channels = *v;
        }
        if (auto v = get_uint_(*props, kTagSampleRate)) {
            _pending_sr = *v;
        }
    }


    // Extract a pointer to const property_map from tag.second, handling ref_wrapper or direct map.
    template <typename TagT>
    static const property_map* get_props_(const TagT& tag) {
        using Second = std::remove_cvref_t<decltype(tag.second)>;

        // second is std::reference_wrapper<const property_map> (or non-const)
        if constexpr (std::is_same_v<Second, std::reference_wrapper<const property_map>> ||
                    std::is_same_v<Second, std::reference_wrapper<property_map>>) {
            return &tag.second.get();
        }
        // second is a (const) property_map by ref/value
        else if constexpr (std::is_same_v<Second, const property_map> ||
                        std::is_same_v<Second, property_map>) {
            return &tag.second;
        }
        // second is a (const) property_map*
        else if constexpr (std::is_pointer_v<Second> &&
                        (std::is_same_v<std::remove_pointer_t<Second>, property_map> ||
                            std::is_same_v<std::remove_pointer_t<Second>, const property_map>)) {
            return tag.second;
        }
        // unknown payload type
        else {
            return nullptr;
        }
    }
    
    static std::optional<uint32_t>
    get_uint_(const property_map& pm, const std::string& key)
    {
        auto it = pm.find(key);
        if (it == pm.end()) return std::nullopt;

        const auto& v = it->second; // rva::variant<...>

        std::optional<uint32_t> out;
        std::visit([&](const auto& x) {
            using X = std::decay_t<decltype(x)>;

            if constexpr (std::is_same_v<X, std::monostate> || std::is_same_v<X, bool>) {
                // ignore
            } else if constexpr (std::is_integral_v<X>) {
                if constexpr (std::is_signed_v<X>)
                    out = x >= 0 ? static_cast<uint32_t>(x) : 0u;
                else
                    out = static_cast<uint32_t>(x);
            } else if constexpr (std::is_floating_point_v<X>) {
                out = x > 0 ? static_cast<uint32_t>(x) : 0u;
            } else if constexpr (std::is_same_v<X, std::string>) {
                try {
                    // allow decimal strings; clamp to u32 range if desired
                    unsigned long tmp = std::stoul(x);
                    out = static_cast<uint32_t>(tmp);
                } catch (...) {
                    // leave out = std::nullopt
                }
            } else {
                // other variant members (vectors, tensors, maps, etc.) -> ignore
            }
        }, v);

        return out;
    }

    // forwarding overload if some code still passes a reference_wrapper:
    static std::optional<uint32_t>
    get_uint_(const std::reference_wrapper<const property_map>& pm, const std::string& key)
    {
        return get_uint_(pm.get(), key);
    }

    bool ready_to_open_() const {
        return _pending_channels > 0 && _pending_sr > 0;
    }
    bool format_changed_() const {
        return _stream_open && (_pending_channels != _channels || _pending_sr != _sr);
    }

    bool open_or_reopen_stream_() {
        try {
            if (_stream_open) close_stream();

            _channels = _pending_channels;
            _sr       = _pending_sr;

            const double lat = std::max(0.010, (double)target_latency_s.value);
            const size_t fifo_samples = std::max(
                (size_t)(lat * _sr) * _channels,
                (size_t)frames_per_buf.value * _channels * 4
            );
            _fifo.reset(fifo_samples);

            RtAudio::StreamParameters o{};
            o.deviceId     = (device_index.value >= 0)
                            ? (unsigned int)device_index.value
                            : _dac.getDefaultOutputDevice();
            o.nChannels    = _channels;
            o.firstChannel = 0;

            RtAudio::StreamOptions opts{};
            // Some RtAudio versions don't have RTAUDIO_DITHER. Guard it.
            #ifdef RTAUDIO_DITHER
            if (dither) opts.flags |= RTAUDIO_DITHER;
            #else
            (void)dither; // avoid unused warning
            #endif
            // You can also set other flags if desired, e.g.:
            // opts.flags |= RTAUDIO_MINIMIZE_LATENCY;

            _dac.openStream(&o, nullptr,
                            RTAUDIO_FLOAT32, _sr,
                            (unsigned int*)&frames_per_buf.value,
                            &RtAudioSink::rtaudio_cb, this, &opts);
            _dac.startStream();

            _stream_open    = true;
            _stream_running = true;
            return true;
        }
        // Some RtAudio builds don’t throw a custom RtAudioError type; catch std::exception.
        catch (const std::exception& e) {
            throw gr::exception(std::format("RtAudioSink open/start error: {}", e.what()));
        }
    }

    void close_stream() {
        if (!_stream_open) return;
        try {
            if (_dac.isStreamRunning()) _dac.stopStream();
            if (_dac.isStreamOpen())    _dac.closeStream();
        } catch (...) {}
        _stream_open = _stream_running = false;
    }
};

} // namespace gr
