#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>

namespace gr::incubator::zeromq::detail {

inline constexpr std::uint16_t kTagHeaderMagic = 0x5FF0;
inline constexpr std::uint8_t  kTagHeaderVersion = 0x01;

struct ZmqTagHeaderRecord {
    std::uint64_t    offset = 0;
    gr::pmt::Value   key{};
    gr::pmt::Value   value{};
    gr::pmt::Value   srcid{};
};

inline void append_bytes(std::vector<std::uint8_t>& out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

template <typename T>
inline void append_native(std::vector<std::uint8_t>& out, T value) {
    append_bytes(out, &value, sizeof(T));
}

template <typename T>
inline T read_native(const std::uint8_t*& ptr, const std::uint8_t* end) {
    if (ptr + sizeof(T) > end) {
        throw std::runtime_error("Truncated GR tag header");
    }
    T value{};
    std::memcpy(&value, ptr, sizeof(T));
    ptr += sizeof(T);
    return value;
}

inline std::vector<std::uint8_t> serialize_tag_header(std::uint64_t offset, const std::vector<ZmqTagHeaderRecord>& tags) {
    std::vector<std::uint8_t> out;
    append_native(out, kTagHeaderMagic);
    append_native(out, kTagHeaderVersion);
    append_native(out, offset);
    append_native(out, static_cast<std::uint64_t>(tags.size()));

    for (const auto& tag : tags) {
        append_native(out, tag.offset);
        const auto key_bytes = legacy_pmt::serialize_to_legacy(tag.key);
        append_bytes(out, key_bytes.data(), key_bytes.size());
        const auto value_bytes = legacy_pmt::serialize_to_legacy(tag.value);
        append_bytes(out, value_bytes.data(), value_bytes.size());
        const auto srcid_bytes = legacy_pmt::serialize_to_legacy(tag.srcid);
        append_bytes(out, srcid_bytes.data(), srcid_bytes.size());
    }

    return out;
}

inline std::size_t parse_tag_header(const std::uint8_t* data,
                                    std::size_t         size,
                                    std::uint64_t&      offset_out,
                                    std::vector<ZmqTagHeaderRecord>& tags_out) {
    if (size < sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t)) {
        throw std::runtime_error("incoming zmq msg too small to hold gr tag header!");
    }

    const std::uint8_t* ptr = data;
    const std::uint8_t* end = data + size;
    const auto header_magic  = read_native<std::uint16_t>(ptr, end);
    const auto header_version = read_native<std::uint8_t>(ptr, end);
    if (header_magic != kTagHeaderMagic) {
        throw std::runtime_error("gr header magic does not match!");
    }
    if (header_version != kTagHeaderVersion) {
        throw std::runtime_error("gr header version too high!");
    }

    offset_out = read_native<std::uint64_t>(ptr, end);
    const auto ntags = read_native<std::uint64_t>(ptr, end);
    tags_out.clear();
    tags_out.reserve(static_cast<std::size_t>(ntags));

    for (std::uint64_t i = 0; i < ntags; ++i) {
        ZmqTagHeaderRecord tag;
        tag.offset = read_native<std::uint64_t>(ptr, end);
        tag.key    = legacy_pmt::deserialize_from_legacy(ptr, end);
        tag.value  = legacy_pmt::deserialize_from_legacy(ptr, end);
        tag.srcid  = legacy_pmt::deserialize_from_legacy(ptr, end);
        tags_out.push_back(std::move(tag));
    }

    return static_cast<std::size_t>(ptr - data);
}

inline std::optional<ZmqTagHeaderRecord> tag_record_from_property_map(const gr::property_map& map) {
    const auto key_it = map.find("key");
    const auto value_it = map.find("value");
    if (key_it == map.end() || value_it == map.end()) {
        return std::nullopt;
    }

    ZmqTagHeaderRecord record;
    record.key = key_it->second;
    record.value = value_it->second;
    if (const auto srcid_it = map.find("srcid"); srcid_it != map.end()) {
        record.srcid = srcid_it->second;
    }
    return record;
}

inline gr::property_map tag_map_from_record(const ZmqTagHeaderRecord& record) {
    gr::property_map map{std::pmr::get_default_resource()};
    map.emplace("key", record.key);
    map.emplace("value", record.value);
    map.emplace("srcid", record.srcid);
    return map;
}

template <typename InputSpanLikeT>
inline std::vector<ZmqTagHeaderRecord> collect_tag_records(InputSpanLikeT& inData) {
    std::vector<ZmqTagHeaderRecord> tags;
    for (const auto& [relIndex, tagMapRef] : inData.tags()) {
        if (relIndex < 0) {
            continue;
        }
        auto rec = tag_record_from_property_map(tagMapRef.get());
        if (!rec.has_value()) {
            continue;
        }
        rec->offset = static_cast<std::uint64_t>(relIndex);
        tags.push_back(std::move(*rec));
    }
    return tags;
}

template <typename OutputSpanLikeT>
inline void publish_tag_records(OutputSpanLikeT& outputSpan, std::size_t consumed, const std::vector<ZmqTagHeaderRecord>& tags) {
    for (const auto& tag : tags) {
        if (tag.offset >= consumed) {
            auto map = tag_map_from_record(tag);
            outputSpan.publishTag(std::move(map), static_cast<std::size_t>(tag.offset - consumed));
        }
    }
}

} // namespace gr::incubator::zeromq::detail
