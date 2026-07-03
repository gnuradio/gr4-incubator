#pragma once

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <zmq.hpp>

namespace gr::incubator::zeromq::detail {

class ZmqSocketTransport {
public:
    explicit ZmqSocketTransport(zmq::socket_type type) : _socket(_context, type) {}

    ZmqSocketTransport(const ZmqSocketTransport&) = delete;
    ZmqSocketTransport& operator=(const ZmqSocketTransport&) = delete;
    ZmqSocketTransport(ZmqSocketTransport&&) = delete;
    ZmqSocketTransport& operator=(ZmqSocketTransport&&) = delete;

    ~ZmqSocketTransport() { close(); }

    void open(const std::string& endpoint, bool bind, int linger, int hwm, bool sink) {
        set_linger(linger);
        set_hwm(hwm, sink);

        if (bind) {
            _socket.bind(endpoint);
        } else {
            _socket.connect(endpoint);
        }
    }

    [[nodiscard]] bool wait_readable(int timeout_ms) {
        zmq::pollitem_t items[] = {{static_cast<void*>(_socket), 0, ZMQ_POLLIN, 0}};
        zmq::poll(&items[0], 1, std::chrono::milliseconds{timeout_ms});
        return (items[0].revents & ZMQ_POLLIN) != 0;
    }

    [[nodiscard]] bool wait_writable(int timeout_ms) {
        zmq::pollitem_t items[] = {{static_cast<void*>(_socket), 0, ZMQ_POLLOUT, 0}};
        zmq::poll(&items[0], 1, std::chrono::milliseconds{timeout_ms});
        return (items[0].revents & ZMQ_POLLOUT) != 0;
    }

    [[nodiscard]] std::string last_endpoint() const { return _socket.get(zmq::sockopt::last_endpoint); }

    [[nodiscard]] zmq::socket_t& socket() { return _socket; }
    [[nodiscard]] const zmq::socket_t& socket() const { return _socket; }

    static bool is_multiple_of(std::size_t byte_count, std::size_t item_size) {
        return item_size != 0 && byte_count % item_size == 0;
    }

    static void require_multiple_of(std::size_t byte_count, std::size_t item_size, const char* kind) {
        if (item_size == 0) {
            throw std::runtime_error(std::string(kind) + ": invalid item size");
        }
        if (byte_count % item_size != 0) {
            throw std::runtime_error(std::string(kind) + ": incoming message size is not a multiple of the item size");
        }
    }

    void close() noexcept {
        if (_closed) {
            return;
        }

        try {
            if constexpr (requires(zmq::context_t& ctx) { ctx.shutdown(); }) {
                _context.shutdown();
            }
        } catch (...) {
        }

        try {
            _socket.close();
        } catch (...) {
        }

        _closed = true;
    }

private:
    void set_linger(int linger) {
        _socket.set(zmq::sockopt::linger, linger);
    }

    void set_hwm(int hwm, bool sink) {
        if (hwm < 0) {
            return;
        }
        if (sink) {
            _socket.set(zmq::sockopt::sndhwm, hwm);
        } else {
            _socket.set(zmq::sockopt::rcvhwm, hwm);
        }
    }

    zmq::context_t _context{1};
    zmq::socket_t   _socket;
    bool            _closed = false;
};

} // namespace gr::incubator::zeromq::detail
