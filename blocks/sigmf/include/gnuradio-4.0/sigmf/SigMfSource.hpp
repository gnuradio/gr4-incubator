#pragma once

#include <algorithm>
#include <complex>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/meta/utils.hpp>

#include <gnuradio-4.0/sigmf/SigMfMetadata.hpp>
#include <gnuradio-4.0/sigmf/detail/SigMfDecode.hpp>
#include <gnuradio-4.0/sigmf/detail/SigMfTagMap.hpp>
#include <gnuradio-4.0/sigmf/detail/SigMfTagSchedule.hpp>

namespace gr::incubator::sigmf {

GR_REGISTER_BLOCK(gr::incubator::sigmf::SigMFSource, [T], [ float, std::complex<float> ])

template<typename T>
struct SigMFSource : Block<SigMFSource<T>> {
    static_assert(detail::SigMfSourceSample<T>, "SigMFSource is currently rf32_le/cf32_le only");

    using Description = Doc<R""(SigMF source block for rf32_le and cf32_le recordings.
Reads the associated .sigmf-meta / .sigmf-data pair and emits the binary sample stream plus GR4-native tags for global, capture, and annotation metadata.
The selected playback window is defined by offset and length; repeat loops that window and replays its tags.)"">;

    PortOut<T> out;

    Annotated<std::string, "file name", Visible, Doc<"Path to the .sigmf-meta file; the .sigmf-data companion is resolved automatically">> file_name;
    Annotated<bool, "repeat", Doc<"Loop the selected playback window and replay its tags">> repeat = false;
    Annotated<gr::Size_t, "offset", Visible, Doc<"Start offset in input samples; emitted tags are rebased from this point">> offset = 0U;
    Annotated<gr::Size_t, "length", Visible, Doc<"Maximum samples to read from the selected window (0: to EOF)">> length = 0U;

    GR_MAKE_REFLECTABLE(SigMFSource, out, file_name, repeat, offset, length);

    SigMfMetadata             _metadata{};
    std::ifstream             _data;
    std::vector<std::uint8_t> _scratch;
    std::vector<detail::SigMfScheduledTag> _scheduled_tags;
    std::size_t                            _next_tag_index = 0UZ;
    std::size_t                            _total_samples = 0UZ;
    std::size_t                            _range_start   = 0UZ;
    std::size_t                            _range_end     = 0UZ; // exclusive
    std::size_t                            _cursor        = 0UZ; // absolute sample index in file
    bool                                   _open          = false;
    bool                                   _done          = false;

    void start() {
        close();

        if (file_name.value.empty()) {
            throw gr::exception("SigMFSource requires file_name");
        }

        auto metadata_exp = loadSigMfMetadata(file_name.value);
        if (!metadata_exp) {
            throw gr::exception(metadata_exp.error().message, metadata_exp.error().sourceLocation);
        }
        _metadata = std::move(*metadata_exp);

        const auto expected_datatype = detail::expectedSigMfDatatype<T>();
        if (_metadata.datatype != expected_datatype) {
            throw gr::exception(std::format("SigMFSource<{}> only supports {}; file '{}' declares '{}'", //
                detail::typeName<T>(), detail::datatypeName(expected_datatype), file_name.value, detail::datatypeName(_metadata.datatype)));
        }
        if (_metadata.item_size_bytes != detail::itemSizeBytes(expected_datatype)) {
            throw gr::exception(std::format("SigMFSource item size mismatch for '{}': expected {}, got {}", //
                file_name.value, sizeof(T), _metadata.item_size_bytes));
        }

        _total_samples = static_cast<std::size_t>(_metadata.data_file_size_bytes / _metadata.item_size_bytes);
        _range_start   = std::min<std::size_t>(static_cast<std::size_t>(offset.value), _total_samples);
        if (_range_start > _total_samples) {
            throw gr::exception(std::format("SigMFSource offset {} exceeds available samples {}", _range_start, _total_samples));
        }

        const std::size_t available = _total_samples - _range_start;
        const std::size_t selected   = (length.value == 0U) ? available : std::min<std::size_t>(static_cast<std::size_t>(length.value), available);
        _range_end                   = _range_start + selected;
        _cursor                      = _range_start;
        _done                        = (selected == 0UZ);

        _data.open(_metadata.data_path, std::ios::binary);
        if (!_data.is_open()) {
            throw gr::exception(std::format("failed to open SigMF data file '{}'", _metadata.data_path.string()));
        }

        _scratch.clear();
        _scratch.resize(_metadata.item_size_bytes);
        _scheduled_tags = detail::buildSigMfTagSchedule(_metadata, _range_start, _range_end, [this](std::string_view trigger_name) { return outputTagMap(trigger_name); });
        _next_tag_index = 0UZ;
        _open = true;

        if (!_done) {
            seekToSample(_cursor);
        }
    }

    void stop() { close(); }

    [[nodiscard]] work::Status processBulk(OutputSpanLike auto& output) {
        if (!_open) {
            return work::Status::DONE;
        }
        if (_done) {
            output.publish(0UZ);
            return work::Status::DONE;
        }
        if (output.empty()) {
            return work::Status::INSUFFICIENT_OUTPUT_ITEMS;
        }

        const std::size_t item_size = _metadata.item_size_bytes;
        std::size_t       produced  = 0UZ;

        while (produced < output.size()) {
            if (_cursor >= _range_end) {
                if (!repeat.value || _range_start == _range_end) {
                    _done = true;
                    break;
                }
                _cursor = _range_start;
                _next_tag_index = 0UZ;
                seekToSample(_cursor);
            }

            const std::size_t remaining_in_range = _range_end - _cursor;
            const std::size_t remaining_out      = output.size() - produced;
            emitReadyTags(output, produced);

            const std::size_t to_read = std::min(nextReadSize(remaining_in_range, remaining_out), remaining_out);
            if (to_read == 0UZ) {
                break;
            }

            const std::size_t bytes_to_read = to_read * item_size;
            _scratch.resize(bytes_to_read);
            _data.read(reinterpret_cast<char*>(_scratch.data()), static_cast<std::streamsize>(bytes_to_read));
            const auto bytes_read = static_cast<std::size_t>(_data.gcount());
            if (bytes_read != bytes_to_read) {
                throw gr::exception(std::format("SigMFSource short read from '{}': expected {} bytes, got {}", _metadata.data_path.string(), bytes_to_read, bytes_read));
            }

            auto* dst = output.data() + produced;
            for (std::size_t i = 0; i < to_read; ++i) {
                dst[i] = detail::decodeSigMfSample<T>(_scratch.data() + (i * item_size));
            }

            produced += to_read;
            _cursor += to_read;
        }

        output.publish(produced);
        if (_done) {
            closeDataOnly();
            return work::Status::DONE;
        }
        return work::Status::OK;
    }

private:
    [[nodiscard]] property_map outputTagMap(std::string_view triggerName) const {
        auto tagMap = out.makeTagMap();
        tag::put(tagMap, tag::TRIGGER_NAME, std::string(triggerName));
        return tagMap;
    }

    [[nodiscard]] std::size_t nextReadSize(std::size_t remaining_in_range, std::size_t remaining_out) const {
        if (_next_tag_index >= _scheduled_tags.size()) {
            return std::min(remaining_in_range, remaining_out);
        }

        const std::size_t loop_pos = _cursor - _range_start;
        const std::size_t next_tag_offset = _scheduled_tags[_next_tag_index].offset;
        if (next_tag_offset <= loop_pos) {
            return 0UZ;
        }
        return std::min({remaining_in_range, remaining_out, next_tag_offset - loop_pos});
    }

    void emitReadyTags(OutputSpanLike auto& output, std::size_t produced) {
        const std::size_t loop_pos = _cursor - _range_start;
        while (_next_tag_index < _scheduled_tags.size() && _scheduled_tags[_next_tag_index].offset == loop_pos) {
            output.publishTag(_scheduled_tags[_next_tag_index].map, produced);
            ++_next_tag_index;
        }
    }

    void seekToSample(std::size_t sample_index) {
        const auto byte_offset = static_cast<std::streamoff>(sample_index * _metadata.item_size_bytes);
        _data.clear();
        _data.seekg(byte_offset, std::ios::beg);
        if (!_data.good()) {
            throw gr::exception(std::format("SigMFSource failed to seek to sample {} in '{}'", sample_index, _metadata.data_path.string()));
        }
    }

    void closeDataOnly() {
        if (_data.is_open()) {
            _data.close();
        }
        _open = false;
    }

    void close() {
        closeDataOnly();
        _done        = false;
        _next_tag_index = 0UZ;
        _cursor      = 0UZ;
        _range_start = 0UZ;
        _range_end   = 0UZ;
        _total_samples = 0UZ;
        _scratch.clear();
        _scheduled_tags.clear();
    }
};

} // namespace gr::incubator::sigmf
