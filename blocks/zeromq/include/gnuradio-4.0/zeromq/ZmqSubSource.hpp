#pragma once

#include "detail/ZmqCommon.hpp"
#include "trait_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <gnuradio-4.0/meta/reflection.hpp>
#include <vector>

namespace gr::incubator::zeromq {

template<typename T>
concept ZmqSubSourceAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same_v<T, gr::pmt::Value>;

template<ZmqSubSourceAcceptableTypes T>
class ZmqSubSource : public gr::Block<ZmqSubSource<T>> {
public:
    using Description = Doc<R""(
@brief ZMQ SUB Source.

This block receives messages on a ZMQ SUB socket and converts them to type T.

)"">;

public:
    gr::PortOut<T> out;
    std::string    endpoint = "tcp://*:5555";
    int            timeout  = 100;
    bool           bind     = false;
    bool           pass_tags = false;
    int            linger   = 1000;
    int            hwm      = -1;
    std::string    key;

    detail::ZmqSocketTransport _transport{zmq::socket_type::sub};

    [[maybe_unused]] std::vector<T> _pending_items;

    GR_MAKE_REFLECTABLE(ZmqSubSource, out, endpoint, timeout, bind, pass_tags, linger, hwm, key);

    void start() {
        _transport.open(endpoint, bind, linger, hwm, false);
        _transport.socket().set(zmq::sockopt::subscribe, key);
    }

    [[nodiscard]] std::string last_endpoint() const { return _transport.last_endpoint(); }

    [[nodiscard]] work::Status processBulk(OutputSpanLike auto& outputSpan) {
        const std::size_t nProcessOut = outputSpan.size();
        size_t npublished = 0;

        if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            for (std::size_t i = 0; i < nProcessOut; ++i) {
                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t msg;
                [[maybe_unused]] const bool ok = bool(_transport.socket().recv(msg));
                if (!ok) {
                    break;
                }

                if (_transport.socket().get(zmq::sockopt::rcvmore)) {
                    zmq::message_t payload;
                    [[maybe_unused]] const bool payload_ok = bool(_transport.socket().recv(payload));
                    if (!payload_ok) {
                        break;
                    }
                    detail::ZmqSocketTransport::require_multiple_of(payload.size(), sizeof(typename T::value_type), "ZmqSubSource vector payload");
                    auto& vec = outputSpan[i];
                    size_t nels = payload.size() / sizeof(typename T::value_type);
                    vec.resize(nels);
                    std::copy(static_cast<typename T::value_type*>(payload.data()), static_cast<typename T::value_type*>(payload.data()) + nels, vec.begin());
                } else {
                    detail::ZmqSocketTransport::require_multiple_of(msg.size(), sizeof(typename T::value_type), "ZmqSubSource vector payload");
                    auto& vec = outputSpan[i];
                    size_t nels = msg.size() / sizeof(typename T::value_type);
                    vec.resize(nels);
                    std::copy(static_cast<typename T::value_type*>(msg.data()), static_cast<typename T::value_type*>(msg.data()) + nels, vec.begin());
                }

                ++npublished;
            }
        } else if constexpr (is_arithmetic_or_complex_v<T>) {
            while (true) {
                size_t room_in_span = nProcessOut - npublished;
                if (_pending_items.size()) {
                    auto n = std::min(room_in_span, _pending_items.size());
                    std::copy(_pending_items.data(), _pending_items.data() + n, outputSpan.begin() + npublished);
                    npublished += n;
                    _pending_items = std::vector<T>(_pending_items.begin() + n, _pending_items.end());
                }

                room_in_span = nProcessOut - npublished;
                if (!room_in_span) {
                    break;
                }

                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t msg;
                [[maybe_unused]] const bool ok = bool(_transport.socket().recv(msg));
                if (!ok) {
                    break;
                }

                if (_transport.socket().get(zmq::sockopt::rcvmore)) {
                    zmq::message_t payload;
                    [[maybe_unused]] const bool payload_ok = bool(_transport.socket().recv(payload));
                    if (!payload_ok) {
                        break;
                    }
                    detail::ZmqSocketTransport::require_multiple_of(payload.size(), sizeof(T), "ZmqSubSource scalar payload");
                    size_t nels = payload.size() / sizeof(T);
                    auto   n    = std::min(nels, room_in_span);
                    auto   rem  = nels - n;
                    std::copy(static_cast<T*>(payload.data()), static_cast<T*>(payload.data()) + n, outputSpan.begin() + npublished);
                    npublished += n;
                    if (rem) {
                        auto prev_pending_items_size = _pending_items.size();
                        _pending_items.resize(prev_pending_items_size + rem);
                        std::copy(static_cast<T*>(payload.data()) + n, static_cast<T*>(payload.data()) + n + rem, _pending_items.begin() + prev_pending_items_size);
                        break;
                    }
                } else {
                    detail::ZmqSocketTransport::require_multiple_of(msg.size(), sizeof(T), "ZmqSubSource scalar payload");
                    size_t nels = msg.size() / sizeof(T);
                    auto   n    = std::min(nels, room_in_span);
                    auto   rem  = nels - n;
                    std::copy(static_cast<T*>(msg.data()), static_cast<T*>(msg.data()) + n, outputSpan.begin() + npublished);
                    npublished += n;
                    if (rem) {
                        auto prev_pending_items_size = _pending_items.size();
                        _pending_items.resize(prev_pending_items_size + rem);
                        std::copy(static_cast<T*>(msg.data()) + n, static_cast<T*>(msg.data()) + n + rem, _pending_items.begin() + prev_pending_items_size);
                        break;
                    }
                }
            }
        } else if constexpr (std::is_same_v<T, gr::pmt::Value>) {
            for (std::size_t i = 0; i < nProcessOut; ++i) {
                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t msg;
                [[maybe_unused]] const bool ok = bool(_transport.socket().recv(msg));
                if (!ok) {
                    break;
                }

                if (_transport.socket().get(zmq::sockopt::rcvmore)) {
                    zmq::message_t payload;
                    [[maybe_unused]] const bool payload_ok = bool(_transport.socket().recv(payload));
                    if (!payload_ok) {
                        break;
                    }
                    outputSpan[i] = legacy_pmt::deserialize_from_legacy(static_cast<const uint8_t*>(payload.data()), payload.size());
                } else {
                    outputSpan[i] = legacy_pmt::deserialize_from_legacy(static_cast<const uint8_t*>(msg.data()), msg.size());
                }
                ++npublished;
            }
        }

        outputSpan.publish(npublished);
        return gr::work::Status::OK;
    }
};

} // namespace gr::incubator::zeromq

GR_REGISTER_BLOCK("gr::incubator::zeromq::ZmqSubSource", gr::incubator::zeromq::ZmqSubSource, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float>, std::vector<float>, std::vector<std::complex<float>>, gr::pmt::Value ])
