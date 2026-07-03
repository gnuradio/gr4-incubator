#pragma once

#include "detail/ZmqCommon.hpp"
#include "trait_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <gnuradio-4.0/meta/reflection.hpp>
#include <vector>

namespace gr::incubator::zeromq {

template<typename T>
concept ZmqReqSourceAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same_v<T, gr::pmt::Value>;

template<ZmqReqSourceAcceptableTypes T>
class ZmqReqSource : public gr::Block<ZmqReqSource<T>> {
public:
    using Description = Doc<R""(
@brief ZMQ REQ Source.

This block sends REQ requests and converts the returned ZMQ replies to type T.

)"">;

public:
    gr::PortOut<T> out;
    std::string    endpoint = "tcp://*:5555";
    int            timeout  = 100;
    bool           bind     = false;
    bool           pass_tags = false;
    int            linger   = 1000;
    int            hwm      = -1;

    detail::ZmqSocketTransport _transport{zmq::socket_type::req};

    [[maybe_unused]] std::vector<T> _pending_items;
    bool                            _req_pending = false;

    GR_MAKE_REFLECTABLE(ZmqReqSource, out, endpoint, timeout, bind, pass_tags, linger, hwm);

    void start() {
        _transport.open(endpoint, bind, linger, hwm, false);
    }

    [[nodiscard]] std::string last_endpoint() const { return _transport.last_endpoint(); }

    static constexpr std::size_t request_count_for(std::size_t room) {
        if constexpr (std::is_same_v<T, gr::pmt::Value>) {
            return 1;
        } else {
            return room;
        }
    }

    [[nodiscard]] work::Status processBulk(OutputSpanLike auto& outputSpan) {
        const std::size_t nProcessOut = outputSpan.size();
        std::size_t       npublished  = 0;

        if constexpr (is_arithmetic_or_complex_v<T>) {
            while (true) {
                if (!_pending_items.empty()) {
                    const auto n = std::min(nProcessOut - npublished, _pending_items.size());
                    std::copy(_pending_items.begin(), _pending_items.begin() + static_cast<std::ptrdiff_t>(n), outputSpan.begin() + npublished);
                    npublished += n;
                    _pending_items.erase(_pending_items.begin(), _pending_items.begin() + static_cast<std::ptrdiff_t>(n));
                }

                if (npublished == nProcessOut) {
                    break;
                }

                if (!_req_pending) {
                    if (!_transport.wait_writable(timeout)) {
                        break;
                    }

                    const uint32_t request_size = static_cast<uint32_t>(request_count_for(nProcessOut - npublished));
                    zmq::message_t  request(sizeof(request_size));
                    std::memcpy(request.data(), &request_size, sizeof(request_size));
                    if (!_transport.socket().send(request, zmq::send_flags::dontwait)) {
                        break;
                    }
                    _req_pending = true;
                }

                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t msg;
                if (!bool(_transport.socket().recv(msg))) {
                    break;
                }
                _req_pending = false;

                detail::ZmqSocketTransport::require_multiple_of(msg.size(), sizeof(T), "ZmqReqSource scalar payload");
                const std::size_t nels = msg.size() / sizeof(T);
                const std::size_t n    = std::min(nels, nProcessOut - npublished);
                std::copy(static_cast<T*>(msg.data()), static_cast<T*>(msg.data()) + static_cast<std::ptrdiff_t>(n), outputSpan.begin() + npublished);
                npublished += n;

                if (nels > n) {
                    _pending_items.resize(nels - n);
                    std::copy(static_cast<T*>(msg.data()) + static_cast<std::ptrdiff_t>(n), static_cast<T*>(msg.data()) + static_cast<std::ptrdiff_t>(nels), _pending_items.begin());
                    break;
                }
            }
        } else if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            while (npublished < nProcessOut) {
                if (!_req_pending) {
                    if (!_transport.wait_writable(timeout)) {
                        break;
                    }

                    const uint32_t request_size = static_cast<uint32_t>(request_count_for(nProcessOut - npublished));
                    zmq::message_t  request(sizeof(request_size));
                    std::memcpy(request.data(), &request_size, sizeof(request_size));
                    if (!_transport.socket().send(request, zmq::send_flags::dontwait)) {
                        break;
                    }
                    _req_pending = true;
                }

                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t msg;
                if (!bool(_transport.socket().recv(msg))) {
                    break;
                }
                _req_pending = false;

                detail::ZmqSocketTransport::require_multiple_of(msg.size(), sizeof(typename T::value_type), "ZmqReqSource vector payload");
                auto& vec = outputSpan[npublished];
                const std::size_t nels = msg.size() / sizeof(typename T::value_type);
                vec.resize(nels);
                std::copy(static_cast<typename T::value_type*>(msg.data()), static_cast<typename T::value_type*>(msg.data()) + static_cast<std::ptrdiff_t>(nels), vec.begin());
                ++npublished;
            }
        } else if constexpr (std::is_same_v<T, gr::pmt::Value>) {
            while (npublished < nProcessOut) {
                if (!_req_pending) {
                    if (!_transport.wait_writable(timeout)) {
                        break;
                    }

                    const uint32_t request_size = 1;
                    zmq::message_t  request(sizeof(request_size));
                    std::memcpy(request.data(), &request_size, sizeof(request_size));
                    if (!_transport.socket().send(request, zmq::send_flags::dontwait)) {
                        break;
                    }
                    _req_pending = true;
                }

                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t msg;
                if (!bool(_transport.socket().recv(msg))) {
                    break;
                }
                _req_pending = false;
                try {
                    outputSpan[npublished] = legacy_pmt::deserialize_from_legacy(static_cast<const uint8_t*>(msg.data()), msg.size());
                    ++npublished;
                } catch (...) {
                    outputSpan.publish(npublished);
                    return gr::work::Status::ERROR;
                }
            }
        }

        outputSpan.publish(npublished);
        return gr::work::Status::OK;
    }
};

} // namespace gr::incubator::zeromq

GR_REGISTER_BLOCK("gr::incubator::zeromq::ZmqReqSource", gr::incubator::zeromq::ZmqReqSource, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float>, std::vector<float>, std::vector<std::complex<float>>, gr::pmt::Value ])
