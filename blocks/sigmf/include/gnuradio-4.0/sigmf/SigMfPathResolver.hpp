#pragma once

#include <gnuradio-4.0/Message.hpp>

#include <cstdint>
#include <filesystem>
#include <expected>
#include <format>

namespace gr::incubator::sigmf {

struct SigMfPaths {
    std::filesystem::path meta_path;
    std::filesystem::path data_path;
};

[[nodiscard]] inline std::expected<std::filesystem::path, gr::Error> resolveSigMfDataPath(const std::filesystem::path& meta_path) {
    if (meta_path.empty()) {
        return std::unexpected(gr::Error{"SigMF metadata path is empty"});
    }

    if (meta_path.extension().string() != ".sigmf-meta") {
        return std::unexpected(gr::Error{std::format("SigMF metadata file must end in '.sigmf-meta' (got '{}')", meta_path.string())});
    }

    auto data_path = meta_path;
    data_path.replace_extension(".sigmf-data");

    if (!std::filesystem::exists(data_path)) {
        return std::unexpected(gr::Error{std::format("SigMF data file '{}' does not exist", data_path.string())});
    }
    if (!std::filesystem::is_regular_file(data_path)) {
        return std::unexpected(gr::Error{std::format("SigMF data path '{}' is not a regular file", data_path.string())});
    }

    return data_path;
}

[[nodiscard]] inline std::expected<std::uintmax_t, gr::Error> validateSigMfDataSize(const std::filesystem::path& data_path, std::size_t item_size_bytes) {
    if (item_size_bytes == 0UZ) {
        return std::unexpected(gr::Error{"SigMF item size must be non-zero"});
    }
    if (!std::filesystem::exists(data_path)) {
        return std::unexpected(gr::Error{std::format("SigMF data file '{}' does not exist", data_path.string())});
    }
    if (!std::filesystem::is_regular_file(data_path)) {
        return std::unexpected(gr::Error{std::format("SigMF data path '{}' is not a regular file", data_path.string())});
    }

    const auto file_size = std::filesystem::file_size(data_path);
    if (file_size % item_size_bytes != 0UZ) {
        return std::unexpected(gr::Error{std::format(
            "SigMF data file '{}' size ({}) is not a multiple of item size ({})",
            data_path.string(),
            file_size,
            item_size_bytes)});
    }
    return file_size;
}

} // namespace gr::incubator::sigmf
