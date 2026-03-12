#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <httplib.h>

#include <algorithm>
#include <concepts>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gr::incubator::http {

namespace detail {

template<typename T>
concept SupportedSample = std::same_as<T, float> || std::same_as<T, std::complex<float>>;

template<SupportedSample T>
class TimeSeriesWindow {
public:
    explicit TimeSeriesWindow(std::size_t channels = 1UZ, std::size_t window_size = 1024UZ) {
        configure(channels, window_size);
    }

    void configure(std::size_t channels, std::size_t window_size) {
        std::lock_guard lock(_mutex);
        _channels   = std::max<std::size_t>(1UZ, channels);
        _windowSize = std::max<std::size_t>(1UZ, window_size);
        _ring.assign(_channels * _windowSize, T{});
        _pending.clear();
        _writeIndex = 0UZ;
        _filled     = 0UZ;
    }

    [[nodiscard]] std::size_t channels() const {
        std::lock_guard lock(_mutex);
        return _channels;
    }

    [[nodiscard]] std::size_t windowSize() const {
        std::lock_guard lock(_mutex);
        return _windowSize;
    }

    void pushInterleaved(std::span<const T> input) {
        if (input.empty()) {
            return;
        }

        std::lock_guard lock(_mutex);
        _pending.insert(_pending.end(), input.begin(), input.end());

        const std::size_t frames = _pending.size() / _channels;
        for (std::size_t frame = 0UZ; frame < frames; ++frame) {
            for (std::size_t ch = 0UZ; ch < _channels; ++ch) {
                const std::size_t src_index = frame * _channels + ch;
                _ring[ch * _windowSize + _writeIndex] = _pending[src_index];
            }
            _writeIndex = (_writeIndex + 1UZ) % _windowSize;
            if (_filled < _windowSize) {
                ++_filled;
            }
        }

        const std::size_t consumed = frames * _channels;
        if (consumed > 0UZ) {
            _pending.erase(_pending.begin(), _pending.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
    }

    [[nodiscard]] std::string snapshotJson() const {
        std::vector<std::vector<T>> per_channel;
        std::size_t                 channels = 0UZ;
        std::size_t                 samples_per_channel = 0UZ;

        {
            std::lock_guard lock(_mutex);
            channels            = _channels;
            samples_per_channel = _filled;
            per_channel.assign(channels, std::vector<T>(samples_per_channel));

            const std::size_t oldest = (_filled == _windowSize) ? _writeIndex : 0UZ;
            for (std::size_t ch = 0UZ; ch < channels; ++ch) {
                for (std::size_t i = 0UZ; i < samples_per_channel; ++i) {
                    const std::size_t idx = (oldest + i) % _windowSize;
                    per_channel[ch][i]    = _ring[ch * _windowSize + idx];
                }
            }
        }

        std::ostringstream os;
        os << std::setprecision(9);

        if constexpr (std::same_as<T, float>) {
            os << "{\"sample_type\":\"float32\",";
            os << "\"channels\":" << channels << ",";
            os << "\"samples_per_channel\":" << samples_per_channel << ",";
            os << "\"layout\":\"channels_first\",";
            os << "\"data\":[";
            for (std::size_t ch = 0UZ; ch < channels; ++ch) {
                if (ch > 0UZ) {
                    os << ',';
                }
                os << '[';
                for (std::size_t i = 0UZ; i < samples_per_channel; ++i) {
                    if (i > 0UZ) {
                        os << ',';
                    }
                    os << per_channel[ch][i];
                }
                os << ']';
            }
            os << "]}";
        } else {
            os << "{\"sample_type\":\"complex64\",";
            os << "\"channels\":" << channels << ",";
            os << "\"samples_per_channel\":" << samples_per_channel << ",";
            os << "\"layout\":\"channels_first_interleaved_complex\",";
            os << "\"data\":[";
            for (std::size_t ch = 0UZ; ch < channels; ++ch) {
                if (ch > 0UZ) {
                    os << ',';
                }
                os << '[';
                for (std::size_t i = 0UZ; i < samples_per_channel; ++i) {
                    if (i > 0UZ) {
                        os << ',';
                    }
                    os << per_channel[ch][i].real() << ',' << per_channel[ch][i].imag();
                }
                os << ']';
            }
            os << "]}";
        }

        return os.str();
    }

private:
    mutable std::mutex _mutex;
    std::size_t        _channels   = 1UZ;
    std::size_t        _windowSize = 1024UZ;
    std::vector<T>     _ring;
    std::vector<T>     _pending;
    std::size_t        _writeIndex = 0UZ;
    std::size_t        _filled     = 0UZ;
};

class SnapshotHttpService {
public:
    using JsonProvider = std::function<std::string()>;

    ~SnapshotHttpService() { stop(); }

    [[nodiscard]] bool start(const std::string& host, std::uint16_t port, const std::string& snapshot_path, JsonProvider provider) {
        stop();

        _host         = host.empty() ? std::string("127.0.0.1") : host;
        _port         = port;
        _boundPort    = 0U;
        _snapshotPath = snapshot_path.empty() ? std::string("/snapshot") : snapshot_path;
        _provider     = std::move(provider);

        _server = std::make_unique<httplib::Server>();
        _server->Get(_snapshotPath, [this](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-store");
            res.set_content(_provider ? _provider() : std::string("{}"), "application/json");
        });

        const int bound = _server->bind_to_port(_host, static_cast<int>(_port));
        if (bound < 0) {
            _server.reset();
            return false;
        }
        _boundPort = static_cast<std::uint16_t>(bound);

        _serverThread = std::thread([this]() {
            if (_server) {
                _server->listen_after_bind();
            }
        });
        return true;
    }

    void stop() {
        if (_server) {
            _server->stop();
        }
        if (_serverThread.joinable()) {
            _serverThread.join();
        }
        _server.reset();
    }

    [[nodiscard]] std::uint16_t boundPort() const { return _boundPort; }

private:
    std::string                    _host{"127.0.0.1"};
    std::uint16_t                  _port{8080U};
    std::uint16_t                  _boundPort{0U};
    std::string                    _snapshotPath{"/snapshot"};
    JsonProvider                   _provider;
    std::unique_ptr<httplib::Server> _server;
    std::thread                    _serverThread;
};

} // namespace detail

GR_REGISTER_BLOCK("gr::incubator::http::HttpTimeSeriesSink", gr::incubator::http::HttpTimeSeriesSink, ([T]), [ float, std::complex<float> ])

template<detail::SupportedSample T>
struct HttpTimeSeriesSink : Block<HttpTimeSeriesSink<T>> {
    using Description = Doc<"@brief HTTP snapshot sink with fixed-window ring buffer.">;

    PortIn<T> in;

    Annotated<std::string, "bind_host", Doc<"HTTP bind host">, Visible> bind_host = "127.0.0.1";
    Annotated<std::uint16_t, "bind_port", Doc<"HTTP bind port">, Visible> bind_port = 8080U;
    Annotated<gr::Size_t, "window_size", Doc<"Samples per channel kept in the ring window">, Visible> window_size = 1024UZ;
    Annotated<gr::Size_t, "channels", Doc<"Number of interleaved input channels">, Visible> channels = 1UZ;
    Annotated<std::string, "snapshot_path", Doc<"HTTP endpoint path for JSON snapshot">, Visible> snapshot_path = "/snapshot";

    GR_MAKE_REFLECTABLE(HttpTimeSeriesSink, in, bind_host, bind_port, window_size, channels, snapshot_path);

    using Block<HttpTimeSeriesSink<T>>::Block;

    void start() {
        _window.configure(static_cast<std::size_t>(channels), static_cast<std::size_t>(window_size));
        if (!_http.start(bind_host.value, bind_port, snapshot_path.value, [this]() { return _window.snapshotJson(); })) {
            throw gr::exception(std::format(
                "HttpTimeSeriesSink failed to bind {}:{} for {}",
                bind_host.value,
                bind_port,
                snapshot_path.value));
        }
    }

    void stop() {
        _http.stop();
    }

    void settingsChanged(const property_map& old_settings, const property_map& new_settings) {
        if (new_settings.contains("channels") || new_settings.contains("window_size")) {
            _window.configure(static_cast<std::size_t>(channels), static_cast<std::size_t>(window_size));
        }

        if (new_settings.contains("bind_host") || new_settings.contains("bind_port") || new_settings.contains("snapshot_path")) {
            // Runtime rebind for v1 simplicity.
            if (!_http.start(bind_host.value, bind_port, snapshot_path.value, [this]() { return _window.snapshotJson(); })) {
                throw gr::exception(std::format(
                    "HttpTimeSeriesSink failed to rebind {}:{} for {}",
                    bind_host.value,
                    bind_port,
                    snapshot_path.value));
            }
        }

        (void)old_settings;
    }

    [[nodiscard]] work::Status processBulk(InputSpanLike auto& input) noexcept {
        if (!input.empty()) {
            _window.pushInterleaved(std::span<const T>(input.data(), input.size()));
            std::ignore = input.consume(input.size());
        }
        return work::Status::OK;
    }

private:
    detail::TimeSeriesWindow<T> _window{};
    detail::SnapshotHttpService _http{};
};

} // namespace gr::incubator::http
