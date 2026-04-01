#pragma once

#include <gnuradio-4.0/Message.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gnuradio-4.0/sigmf/SigMfPathResolver.hpp>
#include <gnuradio-4.0/sigmf/detail/SigMfDatatype.hpp>
#include <gnuradio-4.0/sigmf/detail/SigMfDatetime.hpp>

namespace gr::incubator::sigmf {

using Json = nlohmann::json;
using detail::SigMfDatatype;

using detail::datatypeName;
using detail::isComplexDatatype;
using detail::itemSizeBytes;
using detail::parseSigMfDatatype;
using detail::tryParseSigMfDatetimeNs;

struct SigMfCapture {
    std::size_t sample_start{};
    Json        metadata = Json::object();
};

struct SigMfAnnotation {
    std::size_t             sample_start{};
    std::optional<std::size_t> sample_count{};
    Json                     metadata = Json::object();
};

struct SigMfMetadata {
    std::filesystem::path      meta_path;
    std::filesystem::path      data_path;
    SigMfDatatype              datatype = SigMfDatatype::rf32_le;
    std::size_t                item_size_bytes{};
    bool                       complex_samples{};
    std::optional<double>      sample_rate{};
    Json                       global = Json::object();
    std::vector<SigMfCapture>   captures;
    std::vector<SigMfAnnotation> annotations;
    std::uintmax_t             data_file_size_bytes{};
};

namespace detail {

[[nodiscard]] inline std::expected<Json, gr::Error> readJsonFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::unexpected(gr::Error{std::format("failed to open SigMF metadata file '{}'", path.string())});
    }

    try {
        return Json::parse(input, nullptr, true, true);
    } catch (const nlohmann::json::parse_error& ex) {
        return std::unexpected(gr::Error{std::format("failed to parse SigMF JSON '{}': {}", path.string(), ex.what())});
    } catch (const std::exception& ex) {
        return std::unexpected(gr::Error{std::format("unexpected error while parsing SigMF JSON '{}': {}", path.string(), ex.what())});
    }
}

[[nodiscard]] inline std::expected<std::size_t, gr::Error> readRequiredSizeT(const Json& object, std::string_view key, std::string_view context) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::unexpected(gr::Error{std::format("SigMF {} missing required field '{}'", context, key)});
    }
    if (!it->is_number_integer() && !it->is_number_unsigned()) {
        return std::unexpected(gr::Error{std::format("SigMF {} field '{}' must be an integer", context, key)});
    }
    if (it->is_number_integer()) {
        const auto signed_value = it->get<std::int64_t>();
        if (signed_value < 0) {
            return std::unexpected(gr::Error{std::format("SigMF {} field '{}' must be non-negative", context, key)});
        }
        if (static_cast<std::uint64_t>(signed_value) > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return std::unexpected(gr::Error{std::format("SigMF {} field '{}' is too large", context, key)});
        }
        return static_cast<std::size_t>(signed_value);
    }
    const auto unsigned_value = it->get<std::uint64_t>();
    if (unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected(gr::Error{std::format("SigMF {} field '{}' is too large", context, key)});
    }
    return static_cast<std::size_t>(unsigned_value);
}

[[nodiscard]] inline Json stripFields(Json object, std::initializer_list<std::string_view> keys) {
    for (const auto key : keys) {
        object.erase(std::string(key));
    }
    return object;
}

[[nodiscard]] inline std::expected<SigMfCapture, gr::Error> parseCapture(const Json& capture_json, std::size_t index) {
    if (!capture_json.is_object()) {
        return std::unexpected(gr::Error{std::format("SigMF capture[{}] must be an object", index)});
    }

    const auto sample_start_exp = readRequiredSizeT(capture_json, "core:sample_start", "capture");
    if (!sample_start_exp) {
        return std::unexpected(sample_start_exp.error());
    }
    auto       metadata     = stripFields(capture_json, {"core:sample_start"});
    return SigMfCapture{*sample_start_exp, std::move(metadata)};
}

[[nodiscard]] inline std::expected<SigMfAnnotation, gr::Error> parseAnnotation(const Json& annotation_json, std::size_t index) {
    if (!annotation_json.is_object()) {
        return std::unexpected(gr::Error{std::format("SigMF annotation[{}] must be an object", index)});
    }

    const auto sample_start_exp = readRequiredSizeT(annotation_json, "core:sample_start", "annotation");
    if (!sample_start_exp) {
        return std::unexpected(sample_start_exp.error());
    }

    std::optional<std::size_t> sample_count{};
    if (const auto it = annotation_json.find("core:sample_count"); it != annotation_json.end()) {
        if (!it->is_number_integer() && !it->is_number_unsigned()) {
            return std::unexpected(gr::Error{std::format("SigMF annotation[{}] field 'core:sample_count' must be an integer", index)});
        }
        if (it->is_number_integer()) {
            const auto value = it->get<std::int64_t>();
            if (value < 0) {
                return std::unexpected(gr::Error{std::format("SigMF annotation[{}] field 'core:sample_count' must be non-negative", index)});
            }
            if (static_cast<std::uint64_t>(value) > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
                return std::unexpected(gr::Error{std::format("SigMF annotation[{}] field 'core:sample_count' is too large", index)});
            }
            sample_count = static_cast<std::size_t>(value);
        } else {
            const auto value = it->get<std::uint64_t>();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
                return std::unexpected(gr::Error{std::format("SigMF annotation[{}] field 'core:sample_count' is too large", index)});
            }
            sample_count = static_cast<std::size_t>(value);
        }
    }

    auto metadata = stripFields(annotation_json, {"core:sample_start", "core:sample_count"});
    return SigMfAnnotation{*sample_start_exp, sample_count, std::move(metadata)};
}

} // namespace detail

[[nodiscard]] inline std::expected<SigMfMetadata, gr::Error> parseSigMfMetadata(const Json& root, const std::filesystem::path& meta_path = {}) {
    if (!root.is_object()) {
        return std::unexpected(gr::Error{"SigMF metadata root must be a JSON object"});
    }

    const auto global_it = root.find("global");
    if (global_it == root.end()) {
        return std::unexpected(gr::Error{"SigMF metadata is missing required object 'global'"});
    }
    if (!global_it->is_object()) {
        return std::unexpected(gr::Error{"SigMF field 'global' must be a JSON object"});
    }
    const Json global = *global_it;

    const auto datatype_it = global.find("core:datatype");
    if (datatype_it == global.end()) {
        return std::unexpected(gr::Error{"SigMF global object is missing required field 'core:datatype'"});
    }
    if (!datatype_it->is_string()) {
        return std::unexpected(gr::Error{"SigMF global field 'core:datatype' must be a string"});
    }

    const auto datatype_exp = parseSigMfDatatype(datatype_it->get<std::string>());
    if (!datatype_exp) {
        return std::unexpected(datatype_exp.error());
    }

    std::optional<double> sample_rate{};
    if (const auto sample_rate_it = global.find("core:sample_rate"); sample_rate_it != global.end()) {
        if (!sample_rate_it->is_number()) {
            return std::unexpected(gr::Error{"SigMF global field 'core:sample_rate' must be numeric"});
        }
        sample_rate = sample_rate_it->get<double>();
    }

    const auto captures_it = root.find("captures");
    if (captures_it == root.end()) {
        return std::unexpected(gr::Error{"SigMF metadata is missing required array 'captures'"});
    }
    if (!captures_it->is_array() || captures_it->empty()) {
        return std::unexpected(gr::Error{"SigMF field 'captures' must be a non-empty array"});
    }

    std::vector<SigMfCapture> captures;
    captures.reserve(captures_it->size());
    for (std::size_t i = 0; i < captures_it->size(); ++i) {
        const auto capture_exp = detail::parseCapture((*captures_it)[i], i);
        if (!capture_exp) {
            return std::unexpected(capture_exp.error());
        }
        captures.push_back(std::move(*capture_exp));
    }

    std::vector<SigMfAnnotation> annotations;
    if (const auto annotations_it = root.find("annotations"); annotations_it != root.end()) {
        if (!annotations_it->is_array()) {
            return std::unexpected(gr::Error{"SigMF field 'annotations' must be an array when present"});
        }
        annotations.reserve(annotations_it->size());
        for (std::size_t i = 0; i < annotations_it->size(); ++i) {
            const auto annotation_exp = detail::parseAnnotation((*annotations_it)[i], i);
            if (!annotation_exp) {
                return std::unexpected(annotation_exp.error());
            }
            annotations.push_back(std::move(*annotation_exp));
        }
    }

    SigMfMetadata metadata{};
    metadata.meta_path        = meta_path;
    metadata.datatype         = *datatype_exp;
    metadata.item_size_bytes  = itemSizeBytes(metadata.datatype);
    metadata.complex_samples  = isComplexDatatype(metadata.datatype);
    metadata.sample_rate      = sample_rate;
    metadata.global           = global;
    metadata.captures         = std::move(captures);
    metadata.annotations      = std::move(annotations);

    if (!meta_path.empty()) {
        const auto data_path_exp = resolveSigMfDataPath(meta_path);
        if (!data_path_exp) {
            return std::unexpected(data_path_exp.error());
        }
        metadata.data_path = *data_path_exp;

        const auto data_size_exp = validateSigMfDataSize(metadata.data_path, metadata.item_size_bytes);
        if (!data_size_exp) {
            return std::unexpected(data_size_exp.error());
        }
        metadata.data_file_size_bytes = *data_size_exp;
    }

    return metadata;
}

[[nodiscard]] inline std::expected<SigMfMetadata, gr::Error> parseSigMfMetadata(std::string_view json_text, const std::filesystem::path& meta_path = {}) {
    try {
        return parseSigMfMetadata(Json::parse(std::string(json_text), nullptr, true, true), meta_path);
    } catch (const nlohmann::json::parse_error& ex) {
        return std::unexpected(gr::Error{std::format("failed to parse SigMF JSON: {}", ex.what())});
    } catch (const std::exception& ex) {
        return std::unexpected(gr::Error{std::format("unexpected error while parsing SigMF JSON: {}", ex.what())});
    }
}

[[nodiscard]] inline std::expected<SigMfMetadata, gr::Error> loadSigMfMetadata(const std::filesystem::path& meta_path) {
    if (meta_path.empty()) {
        return std::unexpected(gr::Error{"SigMF metadata path is empty"});
    }
    if (!std::filesystem::exists(meta_path)) {
        return std::unexpected(gr::Error{std::format("SigMF metadata file '{}' does not exist", meta_path.string())});
    }
    if (!std::filesystem::is_regular_file(meta_path)) {
        return std::unexpected(gr::Error{std::format("SigMF metadata path '{}' is not a regular file", meta_path.string())});
    }

    const auto json_exp = detail::readJsonFile(meta_path);
    if (!json_exp) {
        return std::unexpected(json_exp.error());
    }
    return parseSigMfMetadata(*json_exp, meta_path);
}

} // namespace gr::incubator::sigmf
