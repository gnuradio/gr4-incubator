#pragma once

#include <complex>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <string_view>

#include <gnuradio-4.0/Message.hpp>

namespace gr::incubator::sigmf::detail {

// v1 only supports little-endian 32-bit float streams:
// rf32_le maps to float, cf32_le maps to std::complex<float>.
enum class SigMfDatatype {
    rf32_le,
    cf32_le,
};

[[nodiscard]] inline std::string_view datatypeName(SigMfDatatype datatype) {
    switch (datatype) {
    case SigMfDatatype::rf32_le: return "rf32_le";
    case SigMfDatatype::cf32_le: return "cf32_le";
    }
    return "unknown";
}

[[nodiscard]] inline std::size_t itemSizeBytes(SigMfDatatype datatype) {
    switch (datatype) {
    case SigMfDatatype::rf32_le: return sizeof(float);
    case SigMfDatatype::cf32_le: return 2UZ * sizeof(float);
    }
    return 0UZ;
}

[[nodiscard]] inline bool isComplexDatatype(SigMfDatatype datatype) {
    return datatype == SigMfDatatype::cf32_le;
}

[[nodiscard]] inline std::expected<SigMfDatatype, gr::Error> parseSigMfDatatype(std::string_view datatype) {
    if (datatype == "rf32_le") {
        return SigMfDatatype::rf32_le;
    }
    if (datatype == "cf32_le") {
        return SigMfDatatype::cf32_le;
    }
    return std::unexpected(gr::Error{std::format("unsupported SigMF datatype '{}' (supported: rf32_le, cf32_le)", datatype)});
}

template<typename T>
[[nodiscard]] constexpr SigMfDatatype expectedSigMfDatatype() {
    if constexpr (std::same_as<T, float>) {
        return SigMfDatatype::rf32_le;
    } else {
        return SigMfDatatype::cf32_le;
    }
}

template<typename T>
[[nodiscard]] constexpr std::string_view typeName() {
    if constexpr (std::same_as<T, float>) {
        return "float";
    } else {
        return "std::complex<float>";
    }
}

template<typename T>
concept SigMfSourceSample = std::same_as<T, float> || std::same_as<T, std::complex<float>>;

} // namespace gr::incubator::sigmf::detail
