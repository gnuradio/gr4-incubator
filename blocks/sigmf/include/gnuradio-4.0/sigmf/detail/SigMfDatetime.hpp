#pragma once

#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string_view>

#include <gnuradio-4.0/Message.hpp>

#include <nlohmann/json.hpp>

namespace gr::incubator::sigmf::detail {

// trigger_time follows the repo's native tag convention: Unix epoch nanoseconds.
[[nodiscard]] inline std::expected<int, gr::Error> parseDecimalInt(std::string_view text, std::string_view field_name) {
    int value{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::unexpected(gr::Error{std::format("invalid SigMF {} '{}'", field_name, text)});
    }
    return value;
}

[[nodiscard]] inline std::expected<std::uint64_t, gr::Error> parseSigMfDatetimeNs(std::string_view datetime) {
    using namespace std::chrono;

    if (datetime.size() < 20UZ) {
        return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (too short)", datetime)});
    }

    auto require = [&](std::size_t pos, char expected, std::string_view what) -> std::expected<void, gr::Error> {
        if (pos >= datetime.size() || datetime[pos] != expected) {
            return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (expected '{}' for {})", datetime, expected, what)});
        }
        return {};
    };

    const auto parsePart = [&](std::size_t pos, std::size_t len, std::string_view what) -> std::expected<int, gr::Error> {
        if (pos + len > datetime.size()) {
            return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (missing {})", datetime, what)});
        }
        return parseDecimalInt(datetime.substr(pos, len), what);
    };

    const auto year_exp = parsePart(0UZ, 4UZ, "year");
    if (!year_exp) return std::unexpected(year_exp.error());
    if (const auto ok = require(4UZ, '-', "date separator"); !ok) return std::unexpected(ok.error());
    if (const auto ok = require(7UZ, '-', "date separator"); !ok) return std::unexpected(ok.error());
    if (const auto ok = require(10UZ, 'T', "date/time separator"); !ok) return std::unexpected(ok.error());
    const auto month_exp = parsePart(5UZ, 2UZ, "month");
    const auto day_exp   = parsePart(8UZ, 2UZ, "day");
    const auto hour_exp  = parsePart(11UZ, 2UZ, "hour");
    if (!month_exp) return std::unexpected(month_exp.error());
    if (!day_exp) return std::unexpected(day_exp.error());
    if (!hour_exp) return std::unexpected(hour_exp.error());
    if (const auto ok = require(13UZ, ':', "time separator"); !ok) return std::unexpected(ok.error());
    if (const auto ok = require(16UZ, ':', "time separator"); !ok) return std::unexpected(ok.error());
    const auto minute_exp = parsePart(14UZ, 2UZ, "minute");
    const auto second_exp = parsePart(17UZ, 2UZ, "second");
    if (!minute_exp) return std::unexpected(minute_exp.error());
    if (!second_exp) return std::unexpected(second_exp.error());

    std::size_t pos = 19UZ;
    std::uint64_t frac_ns{};
    if (pos < datetime.size() && datetime[pos] == '.') {
        ++pos;
        const std::size_t frac_start = pos;
        while (pos < datetime.size() && std::isdigit(static_cast<unsigned char>(datetime[pos])) != 0) {
            ++pos;
        }
        if (pos == frac_start) {
            return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (empty fractional seconds)", datetime)});
        }
        std::string_view frac_digits = datetime.substr(frac_start, pos - frac_start);
        if (frac_digits.size() > 9UZ) {
            frac_digits = frac_digits.substr(0UZ, 9UZ);
        }
        const auto frac_exp = parseDecimalInt(frac_digits, "fractional seconds");
        if (!frac_exp) return std::unexpected(frac_exp.error());
        frac_ns = static_cast<std::uint64_t>(*frac_exp);
        for (std::size_t i = frac_digits.size(); i < 9UZ; ++i) {
            frac_ns *= 10ULL;
        }
    }

    int tz_offset_minutes = 0;
    if (pos < datetime.size()) {
        const char tz = datetime[pos++];
        if (tz == 'Z' || tz == 'z') {
            if (pos != datetime.size()) {
                return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (trailing characters after Z)", datetime)});
            }
        } else if (tz == '+' || tz == '-') {
            const auto tz_hour_exp = parsePart(pos, 2UZ, "timezone hour");
            if (!tz_hour_exp) return std::unexpected(tz_hour_exp.error());
            pos += 2UZ;
            if (pos < datetime.size() && datetime[pos] == ':') {
                ++pos;
            }
            const auto tz_min_exp = parsePart(pos, 2UZ, "timezone minute");
            if (!tz_min_exp) return std::unexpected(tz_min_exp.error());
            pos += 2UZ;
            if (pos != datetime.size()) {
                return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (trailing characters after timezone)", datetime)});
            }
            if (*tz_hour_exp < 0 || *tz_hour_exp > 23 || *tz_min_exp < 0 || *tz_min_exp > 59) {
                return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (timezone out of range)", datetime)});
            }
            tz_offset_minutes = *tz_hour_exp * 60 + *tz_min_exp;
            if (tz == '-') {
                tz_offset_minutes = -tz_offset_minutes;
            }
        } else {
            return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (expected timezone)", datetime)});
        }
    } else {
        return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (missing timezone)", datetime)});
    }

    const std::chrono::year  y{*year_exp};
    const std::chrono::month m{static_cast<unsigned>(*month_exp)};
    const std::chrono::day   d{static_cast<unsigned>(*day_exp)};
    const year_month_day ymd{y, m, d};
    if (!ymd.ok()) {
        return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (bad calendar date)", datetime)});
    }

    if (*hour_exp < 0 || *hour_exp > 23 || *minute_exp < 0 || *minute_exp > 59 || *second_exp < 0 || *second_exp > 60) {
        return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (time out of range)", datetime)});
    }

    auto tp = sys_days{ymd} + hours{*hour_exp} + minutes{*minute_exp} + seconds{*second_exp} + nanoseconds{frac_ns};
    tp -= minutes{tz_offset_minutes};

    const auto ns = duration_cast<nanoseconds>(tp.time_since_epoch()).count();
    if (ns < 0) {
        return std::unexpected(gr::Error{std::format("invalid SigMF datetime '{}' (before Unix epoch)", datetime)});
    }
    return static_cast<std::uint64_t>(ns);
}

[[nodiscard]] inline std::optional<std::uint64_t> tryParseSigMfDatetimeNs(const nlohmann::json& object) {
    const auto it = object.find("core:datetime");
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }

    const auto parsed = parseSigMfDatetimeNs(it->get<std::string_view>());
    if (!parsed) {
        return std::nullopt;
    }
    return *parsed;
}

} // namespace gr::incubator::sigmf::detail
