#pragma once


// AudioFileSource.hpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <format>
#include <chrono>
#include <type_traits>

// dr_libs (implementation macros must appear exactly once in a .cpp TU)
#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#include "dr_wav.h"
#include "dr_mp3.h"
#include "dr_flac.h"


#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>


namespace gr::audio {

// ---------- Small runtime reader abstraction ----------
struct PCMReader {
    virtual ~PCMReader() = default;
    virtual bool     open(const std::string& path) = 0;
    virtual void     close() = 0;
    virtual uint32_t channels() const = 0;
    virtual uint32_t sample_rate() const = 0;
    virtual uint64_t total_frames() const = 0;            // may be 0/unknown for some formats
    virtual uint64_t read_frames_f32(float* dst, uint64_t max_frames) = 0;
    virtual bool     seek_frame(uint64_t frame_off) = 0;  // 0 = start
};

struct WAVReader final : PCMReader {
    drwav wav{}; bool ok=false;
    bool open(const std::string& p) override {
        ok = drwav_init_file(&wav, p.c_str(), nullptr) != DRWAV_FALSE;
        return ok;
    }
    void close() override { if (ok) drwav_uninit(&wav); ok=false; }
    uint32_t channels() const override { return ok ? (uint32_t)wav.channels : 0; }
    uint32_t sample_rate() const override { return ok ? (uint32_t)wav.sampleRate : 0; }
    uint64_t total_frames() const override { return ok ? (uint64_t)wav.totalPCMFrameCount : 0ULL; }
    uint64_t read_frames_f32(float* dst, uint64_t max_frames) override {
        if (!ok) return 0;
        return (uint64_t)drwav_read_pcm_frames_f32(&wav, (drwav_uint64)max_frames, dst);
    }
    bool seek_frame(uint64_t frame_off) override {
        if (!ok) return false;
        return drwav_seek_to_pcm_frame(&wav, (drwav_uint64)frame_off) != DRWAV_FALSE;
    }
};

struct MP3Reader final : PCMReader {
    drmp3 mp3{}; bool ok=false;
    bool open(const std::string& p) override {
        ok = drmp3_init_file(&mp3, p.c_str(), nullptr) != DRMP3_FALSE;
        return ok;
    }
    void close() override { if (ok) drmp3_uninit(&mp3); ok=false; }
    uint32_t channels() const override { return ok ? (uint32_t)mp3.channels : 0; }
    uint32_t sample_rate() const override { return ok ? (uint32_t)mp3.sampleRate : 0; }
    uint64_t total_frames() const override { return 0ULL; } // MP3 duration not always known up front
    uint64_t read_frames_f32(float* dst, uint64_t max_frames) override {
        if (!ok) return 0;
        return (uint64_t)drmp3_read_pcm_frames_f32(&mp3, (drmp3_uint64)max_frames, dst);
    }
    bool seek_frame(uint64_t frame_off) override {
        if (!ok) return false;
        return drmp3_seek_to_pcm_frame(&mp3, (drmp3_uint64)frame_off) != DRMP3_FALSE;
    }
};

struct FLACReader final : PCMReader {
    drflac* flac = nullptr;
    bool open(const std::string& p) override {
        flac = drflac_open_file(p.c_str(), nullptr);
        return flac != nullptr;
    }
    void close() override { if (flac) { drflac_close(flac); flac=nullptr; } }
    uint32_t channels() const override { return flac ? (uint32_t)flac->channels : 0; }
    uint32_t sample_rate() const override { return flac ? (uint32_t)flac->sampleRate : 0; }
    uint64_t total_frames() const override { return flac ? (uint64_t)flac->totalPCMFrameCount : 0ULL; }
    uint64_t read_frames_f32(float* dst, uint64_t max_frames) override {
        if (!flac) return 0;
        return (uint64_t)drflac_read_pcm_frames_f32(flac, (drflac_uint64)max_frames, dst);
    }
    bool seek_frame(uint64_t frame_off) override {
        if (!flac) return false;
        return drflac_seek_to_pcm_frame(flac, (drflac_uint64)frame_off) != DRFLAC_FALSE;
    }
};

// ---------- Utility ----------
inline std::string to_lower_ext(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    return ext;
}

inline std::unique_ptr<PCMReader> make_reader_for(const std::string& path) {
    const auto ext = to_lower_ext(path);
    if (ext == ".wav") return std::make_unique<WAVReader>();
    if (ext == ".mp3") return std::make_unique<MP3Reader>();
    if (ext == ".flac")return std::make_unique<FLACReader>();
    // default to WAV if no/unknown extension
    return std::make_unique<WAVReader>();
}

namespace detail {

inline std::vector<std::filesystem::path> getSortedFilesContaining(const std::string& fileName) {
    std::filesystem::path filePath(fileName);
    if (!std::filesystem::exists(filePath.parent_path())) {
        throw gr::exception(std::format("path/file '{}' does not exist.", fileName));
    }

    std::vector<std::filesystem::path> matchingFiles;
    std::copy_if(std::filesystem::directory_iterator(filePath.parent_path()), std::filesystem::directory_iterator{}, std::back_inserter(matchingFiles), //
        [&](const auto& entry) { return entry.is_regular_file() && entry.path().string().find(filePath.filename().string()) != std::string::npos; });

    std::sort(matchingFiles.begin(), matchingFiles.end());
    return matchingFiles;
}
}

template<typename T>
struct AudioFileSource : Block<AudioFileSource<T>> {
    static_assert(std::is_same_v<T,float>, "AudioFileSource is float-only (T=float).");

    // using Description = Doc<R"(@brief Single-file audio source (WAV/MP3/FLAC) → float32 interleaved.
    //     Reads one file using dr_wav/dr_mp3/dr_flac. Supports repeat looping, offset/length
    //     \(in samples\), and an optional start trigger tag.)">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    PortOut<T> out;

    A<std::string, "file name",   Doc<"Full path to the audio file (.wav/.mp3/.flac)">, Visible> file_name;
    A<bool,        "repeat",      Doc<"true: loop the same file at EOF">>                         repeat = false;
    A<gr::Size_t,  "offset",      Doc<"start offset in SAMPLES (interleaved)">,                   Visible> offset = 0U;
    A<gr::Size_t,  "length",      Doc<"max number of SAMPLES to read (0: infinite)">,             Visible> length = 0U;
    A<std::string, "trigger name",Doc<"name of trigger published at first output">>               trigger_name = "AudioFileSource::start";

    GR_MAKE_REFLECTABLE(AudioFileSource, out, file_name, repeat, offset, length, trigger_name);

    // --- State ---
    std::unique_ptr<PCMReader> _reader;
    bool        _open        = false;
    bool        _emittedStart= false;
    uint32_t    _channels    = 0;
    uint32_t    _sampleRate  = 0;
    uint64_t    _totalFrames = 0;
    std::size_t _totalSamplesEmitted = 0;   // across lifetime
    std::size_t _totalSamplesFile    = 0;   // since last (re)open

    // ---- lifecycle ----
    void start() {
        _emittedStart         = false;
        _totalSamplesEmitted  = 0;
        _totalSamplesFile     = 0;

        const auto p = std::filesystem::path(file_name.value);
        if (!std::filesystem::exists(p)) {
            throw gr::exception(std::format("audio file '{}' does not exist.", file_name.value));
        }

        _reader = make_reader_for(file_name.value);
        if (!_reader->open(file_name.value)) {
            throw gr::exception(std::format("failed to open audio file '{}'.", file_name.value));
        }

        _channels    = _reader->channels();
        _sampleRate  = _reader->sample_rate();
        _totalFrames = _reader->total_frames();
        _open        = true;

        // apply offset (samples → frames)
        if (offset.value != 0U && _channels != 0) {
            const uint64_t foff = static_cast<uint64_t>(offset.value) / _channels;
            _reader->seek_frame(foff); // best-effort
        }
    }

    void stop() { close(); }

    [[nodiscard]] constexpr work::Status processBulk(OutputSpanLike auto& dataOut) noexcept {
        if (!_open || _channels == 0) return work::Status::DONE;

        std::size_t out_samples = dataOut.size();  // interleaved float samples

        // enforce length (in samples)
        if (length.value != 0U) {
            const std::size_t left = (length.value > _totalSamplesFile)
                                   ? (length.value - _totalSamplesFile) : 0U;
            out_samples = std::min(out_samples, left);
            if (out_samples == 0U) {
                // length reached for this open; if repeat, rewind and continue next tick
                if (!handle_eof_or_length()) return work::Status::DONE;
                return work::Status::OK;
            }
        }

        const std::size_t frames_req = out_samples / _channels;
        if (frames_req == 0) return work::Status::OK; // buffer smaller than one frame

        float* out_ptr = reinterpret_cast<float*>(dataOut.data());
        const uint64_t frames_read = _reader->read_frames_f32(out_ptr, (uint64_t)frames_req);
        const std::size_t samples_read = (std::size_t)frames_read * _channels;

        if (!_emittedStart && !trigger_name.value.empty() && samples_read > 0) {
            dataOut.publishTag(
                property_map{
                    {std::string(tag::TRIGGER_NAME.shortKey()), trigger_name.value},
                    {std::string(tag::TRIGGER_TIME.shortKey()), settings::convertTimePointToUint64Ns(std::chrono::system_clock::now())},
                    {"num_channels", _channels},
                    {"sample_rate", static_cast<float>(_sampleRate)},
                },
                0UZ
            );
            _emittedStart = true;
        }

        dataOut.publish(samples_read);
        _totalSamplesEmitted += samples_read;
        _totalSamplesFile    += samples_read;

        const bool hit_eof = frames_read < (uint64_t)frames_req;
        const bool hit_len = (length.value != 0U) && (_totalSamplesFile >= length.value);
        if (hit_eof || hit_len) {
            if (!handle_eof_or_length()) return work::Status::DONE;
        }

        return work::Status::OK;
    }

private:
    void close() {
        if (_reader) _reader->close();
        _reader.reset();
        _open = false;
    }

    bool handle_eof_or_length() {
        if (!repeat) { close(); return false; }
        // Loop the same file: seek to start and reset per-file counters/trigger
        if (_reader && _channels != 0) {
            _reader->seek_frame(0);
            _totalSamplesFile = 0;
            _emittedStart = false; // emit trigger again at next non-zero output
            return true;
        }
        return false;
    }
};


} // namespace gr::audio
