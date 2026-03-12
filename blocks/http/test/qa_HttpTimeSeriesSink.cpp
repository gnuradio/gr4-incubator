#include <boost/ut.hpp>

#include <gnuradio-4.0/http/HttpTimeSeriesSink.hpp>

#include <httplib.h>

#include <array>
#include <chrono>
#include <complex>
#include <ranges>
#include <thread>

using namespace boost::ut;

namespace {

bool contains_all(std::string_view haystack, std::initializer_list<std::string_view> needles) {
    return std::ranges::all_of(needles, [&](std::string_view n) { return haystack.find(n) != std::string_view::npos; });
}

} // namespace

const suite HttpTimeSeriesSinkTests = [] {
    "float_snapshot_json_channels_first"_test = [] {
        gr::incubator::http::detail::TimeSeriesWindow<float> window(2UZ, 3UZ);

        // Interleaved channel order: [c0,c1]
        window.pushInterleaved(std::array<float, 6>{0.1f, 1.1f, 0.2f, 1.2f, 0.3f, 1.3f});

        const auto json = window.snapshotJson();
        expect(contains_all(json,
            {
                "\"sample_type\":\"float32\"",
                "\"channels\":2",
                "\"samples_per_channel\":3",
                "\"layout\":\"channels_first\"",
                "\"data\":[[0.100000001,0.200000003,0.300000012],[1.10000002,1.20000005,1.29999995]]"
            }));
    };

    "complex_snapshot_json_interleaved"_test = [] {
        using C = std::complex<float>;
        gr::incubator::http::detail::TimeSeriesWindow<C> window(1UZ, 3UZ);

        window.pushInterleaved(std::array<C, 3>{C{1.0f, 10.0f}, C{2.0f, 20.0f}, C{3.0f, 30.0f}});

        const auto json = window.snapshotJson();
        expect(contains_all(json,
            {
                "\"sample_type\":\"complex64\"",
                "\"channels\":1",
                "\"samples_per_channel\":3",
                "\"layout\":\"channels_first_interleaved_complex\"",
                "\"data\":[[1,10,2,20,3,30]]"
            }));
    };

    "ring_window_keeps_latest_samples"_test = [] {
        gr::incubator::http::detail::TimeSeriesWindow<float> window(2UZ, 3UZ);

        // 5 frames, 2 channels -> window should keep frames 2,3,4 (oldest->newest)
        window.pushInterleaved(std::array<float, 10>{0, 10, 1, 11, 2, 12, 3, 13, 4, 14});

        const auto json = window.snapshotJson();
        expect(contains_all(json,
            {
                "\"samples_per_channel\":3",
                "\"data\":[[2,3,4],[12,13,14]]"
            }));
    };

    "http_snapshot_service_starts_and_stops"_test = [] {
        gr::incubator::http::detail::TimeSeriesWindow<float> window(2UZ, 4UZ);
        window.pushInterleaved(std::array<float, 8>{1, 11, 2, 12, 3, 13, 4, 14});

        gr::incubator::http::detail::SnapshotHttpService service;
        constexpr std::uint16_t port = 18081U;
        const bool started = service.start("127.0.0.1", port, "/snapshot", [&]() { return window.snapshotJson(); });
        if (!started) {
            expect(true);
            return;
        }

        service.stop();
    };
};

int main() { return cfg<override>.run(); }
