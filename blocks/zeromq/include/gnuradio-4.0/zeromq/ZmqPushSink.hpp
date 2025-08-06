#ifndef _GR4_ZEROMQ_ZMQ_PUSH_SINK
#define _GR4_ZEROMQ_ZMQ_PUSH_SINK

#include "trait_helpers.hpp"

#include <cerrno>
#include <format>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/meta/reflection.hpp>
#include <pmtv/pmt.hpp>
#include <pmt_converter/pmt_legacy_codec.h>
#include <zmq.hpp>

namespace gr::zeromq {

GR_REGISTER_BLOCK(gr::zeromq::ZmqPushSink, [uint8_t]);

template<typename T>
concept ZmqPushSinkAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same<T, pmtv::pmt>::value;

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
    zmq::context_t _context;
    zmq::socket_t  _socket = {_context, zmq::socket_type::push};

    GR_MAKE_REFLECTABLE(ZmqPushSink, in, endpoint, timeout, bind);

    void start() {
        if (bind) {
            _socket.bind(endpoint);
        } else {
            _socket.connect(endpoint);
        }
    }


    [[nodiscard]] constexpr work::Status processBulk(InputSpanLike auto& inData) {

        // for vectors
        // FIXME: replace with cleaner type traits - not sure yet where to put them
        if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            for (auto& a : inData) {
                const size_t size_in_bytes = a.size() * sizeof(typename T::value_type);

                zmq::message_t zmsg(size_in_bytes);
                memcpy(zmsg.data(), a.data(), size_in_bytes);
                _socket.send(zmsg, zmq::send_flags::none);
            }
        } else if constexpr(is_arithmetic_or_complex_v<T>) {
            const size_t size_in_bytes = inData.size() * sizeof(T);
            zmq::message_t zmsg(size_in_bytes);
            memcpy(zmsg.data(), inData.data(), size_in_bytes);
            _socket.send(zmsg, zmq::send_flags::none);
        } else if constexpr(std::is_same_v<T,pmtv::pmt>) {

            // convert to legacy pmt, serialize, and push over socket
            for (auto& pmtObj : inData) {
                std::vector<uint8_t> serialized = legacy_pmt::serialize_to_legacy(pmtObj);
                zmq::message_t zmsg(serialized.size());
                memcpy(zmsg.data(), serialized.data(), serialized.size());
                _socket.send(zmsg, zmq::send_flags::none);
            }
            // std::cout << "tmp" << std::endl;
        }

        return gr::work::Status::OK;
    }
};

} // namespace gr::zeromq

#endif // _GR4_ZEROMQ_ZMQ_PUSH_SINK
