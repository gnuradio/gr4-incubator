#pragma once

#include "detail/ZmqCommon.hpp"
#include "detail/ZmqTagHeaders.hpp"
#include "trait_helpers.hpp"

#include <cstring>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <gnuradio-4.0/meta/reflection.hpp>
#include <tuple>
#include <vector>

namespace gr::incubator::zeromq {

template<typename T>
concept ZmqPubSinkAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same_v<T, gr::pmt::Value>;

template<ZmqPubSinkAcceptableTypes T>
class ZmqPubSink : public gr::Block<ZmqPubSink<T>> {
public:
    using Description = Doc<R""(
@brief ZMQ PUB Sink.

This block sends items of type T as ZMQ messages using a PUB socket.

)"">;

public:
    gr::PortIn<T>  in;
    std::string    endpoint = "tcp://*:5555";
    int            timeout  = 100;
    bool           bind     = true;
    bool           pass_tags = false;
    int            linger   = 1000;
    int            hwm      = -1;
    std::string    key;
    bool           drop_on_hwm = true;

    detail::ZmqSocketTransport _transport{zmq::socket_type::pub};

    GR_MAKE_REFLECTABLE(ZmqPubSink, in, endpoint, timeout, bind, pass_tags, linger, hwm, key, drop_on_hwm);

    void start() {
        _transport.open(endpoint, bind, linger, hwm, true);
        const int no_drop = drop_on_hwm ? 0 : 1;
        _transport.socket().set(zmq::sockopt::xpub_nodrop, no_drop);
    }

    [[nodiscard]] std::string last_endpoint() const { return _transport.last_endpoint(); }

    [[nodiscard]] constexpr work::Status processBulk(InputSpanLike auto& inData) {
        if (inData.size() == 0) {
            return gr::work::Status::OK;
        }

        if (!_transport.wait_writable(timeout)) {
            std::ignore = inData.consume(inData.size());
            return gr::work::Status::OK;
        }

        std::size_t consumed = 0;
        auto&       socket = _transport.socket();
        const auto tags = pass_tags ? detail::collect_tag_records(inData) : std::vector<detail::ZmqTagHeaderRecord>{};
        const auto header = pass_tags ? detail::serialize_tag_header(0, tags) : std::vector<std::uint8_t>{};

        auto send_payload = [&](auto&& payload) {
            if (!key.empty()) {
                zmq::message_t key_message(key.size());
                std::memcpy(key_message.data(), key.data(), key.size());
                if (!socket.send(key_message, zmq::send_flags::sndmore | zmq::send_flags::dontwait)) {
                    return false;
                }
            }
            return bool(socket.send(payload, zmq::send_flags::dontwait));
        };

        if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            for (auto& a : inData) {
                const size_t size_in_bytes = a.size() * sizeof(typename T::value_type);

                zmq::message_t zmsg(header.size() + size_in_bytes);
                if (!header.empty()) {
                    std::memcpy(zmsg.data(), header.data(), header.size());
                }
                std::memcpy(static_cast<std::uint8_t*>(zmsg.data()) + header.size(), a.data(), size_in_bytes);
                if (!send_payload(zmsg)) {
                    break;
                }
                ++consumed;
            }
        } else if constexpr (is_arithmetic_or_complex_v<T>) {
            const size_t size_in_bytes = inData.size() * sizeof(T);
            zmq::message_t zmsg(header.size() + size_in_bytes);
            if (!header.empty()) {
                std::memcpy(zmsg.data(), header.data(), header.size());
            }
            std::memcpy(static_cast<std::uint8_t*>(zmsg.data()) + header.size(), inData.data(), size_in_bytes);
            if (send_payload(zmsg)) {
                consumed = inData.size();
            }
        } else if constexpr (std::is_same_v<T, gr::pmt::Value>) {
            for (auto& pmtObj : inData) {
                std::vector<uint8_t> serialized = legacy_pmt::serialize_to_legacy(pmtObj);
                zmq::message_t zmsg(header.size() + serialized.size());
                if (!header.empty()) {
                    std::memcpy(zmsg.data(), header.data(), header.size());
                }
                std::memcpy(static_cast<std::uint8_t*>(zmsg.data()) + header.size(), serialized.data(), serialized.size());
                if (!send_payload(zmsg)) {
                    break;
                }
                ++consumed;
            }
        }

        std::ignore = inData.consume(consumed);
        return gr::work::Status::OK;
    }
};

} // namespace gr::incubator::zeromq

GR_REGISTER_BLOCK("gr::incubator::zeromq::ZmqPubSink", gr::incubator::zeromq::ZmqPubSink, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float>, std::vector<float>, std::vector<std::complex<float>>, gr::pmt::Value ])
