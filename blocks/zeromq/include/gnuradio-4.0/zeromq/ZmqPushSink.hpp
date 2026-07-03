#pragma once

#include "detail/ZmqCommon.hpp"
#include "trait_helpers.hpp"

#include <algorithm>
#include <cstring>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/meta/reflection.hpp>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <tuple>
#include <vector>

namespace gr::incubator::zeromq {

template<typename T>
concept ZmqPushSinkAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same_v<T, gr::pmt::Value>;

template<ZmqPushSinkAcceptableTypes T>
class ZmqPushSink : public gr::Block<ZmqPushSink<T>> {
public:
    using Description = Doc<R""(
@brief ZMQ PUSH Sink.

This block sends items of type T as ZMQ messages using a PUSH socket.

)"">;

public:
    gr::PortIn<T>  in;
    std::string    endpoint = "tcp://*:5555";
    int            timeout  = 100;
    bool           bind     = true;
    bool           pass_tags = false;
    int            linger   = 1000;
    int            hwm      = -1;

    detail::ZmqSocketTransport _transport{zmq::socket_type::push};

    GR_MAKE_REFLECTABLE(ZmqPushSink, in, endpoint, timeout, bind, pass_tags, linger, hwm);

    void start() {
        _transport.open(endpoint, bind, linger, hwm, true);
    }

    [[nodiscard]] std::string last_endpoint() const { return _transport.last_endpoint(); }

    [[nodiscard]] constexpr work::Status processBulk(InputSpanLike auto& inData) {
        if (inData.size() == 0) {
            return gr::work::Status::OK;
        }

        if (!_transport.wait_writable(timeout)) {
            return gr::work::Status::OK;
        }

        std::size_t consumed = 0;

        // for vectors
        // FIXME: replace with cleaner type traits - not sure yet where to put them
        if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            for (auto& a : inData) {
                const size_t size_in_bytes = a.size() * sizeof(typename T::value_type);

                zmq::message_t zmsg(size_in_bytes);
                memcpy(zmsg.data(), a.data(), size_in_bytes);
                if (!_transport.socket().send(zmsg, zmq::send_flags::dontwait)) {
                    break;
                }
                ++consumed;
            }
        } else if constexpr(is_arithmetic_or_complex_v<T>) {
            const size_t size_in_bytes = inData.size() * sizeof(T);
            zmq::message_t zmsg(size_in_bytes);
            memcpy(zmsg.data(), inData.data(), size_in_bytes);
            if (_transport.socket().send(zmsg, zmq::send_flags::dontwait)) {
                consumed = inData.size();
            }
        } else if constexpr(std::is_same_v<T, gr::pmt::Value>) {

            // convert to legacy pmt, serialize, and push over socket
            for (auto& pmtObj : inData) {
                std::vector<uint8_t> serialized = legacy_pmt::serialize_to_legacy(pmtObj);
                zmq::message_t zmsg(serialized.size());
                memcpy(zmsg.data(), serialized.data(), serialized.size());
                if (!_transport.socket().send(zmsg, zmq::send_flags::dontwait)) {
                    break;
                }
                ++consumed;
            }
            // std::cout << "tmp" << std::endl;
        }

        std::ignore = inData.consume(consumed);
        return gr::work::Status::OK;
    }
};

} // namespace gr::incubator::zeromq

GR_REGISTER_BLOCK("gr::incubator::zeromq::ZmqPushSink", gr::incubator::zeromq::ZmqPushSink, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float>, std::vector<float>, std::vector<std::complex<float>>, gr::pmt::Value ])
