#pragma once

#include <cerrno>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/meta/reflection.hpp>
#include "trait_helpers.hpp"
#include <zmq.hpp>
#include <pmt_converter/pmt_legacy_codec.h>


namespace gr::zeromq {

template<typename T>
concept ZmqPullSourceAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same<T, pmtv::pmt>::value;

template<ZmqPullSourceAcceptableTypes T>

class ZmqPullSource : public gr::Block<ZmqPullSource<T>> {
public:
    using Description = Doc<R""(
@brief ZMQ PULL Source.

This block receives ZMQ messages using a PULL socket and converts to type T.

)"">;

public:
    gr::PortOut<T> out;
    std::string    endpoint = "tcp://*:5555";
    int            timeout  = 100;
    bool           bind     = false;
    zmq::context_t _context;
    zmq::socket_t  _socket = {_context, zmq::socket_type::pull};

    [[maybe_unused]] std::vector<T> _pending_items;

    GR_MAKE_REFLECTABLE(ZmqPullSource, out, endpoint, timeout, bind);

    void start() {
        if (bind) {
            _socket.bind(endpoint);
        } else {
            _socket.connect(endpoint);
        }
    }

    [[nodiscard]] work::Status processBulk(OutputSpanLike auto& outputSpan) noexcept {
        const std::size_t nProcessOut = outputSpan.size();

        size_t npublished = 0;

        if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            for (std::size_t i = 0; i < nProcessOut; ++i) {
                zmq::pollitem_t items[] = {{static_cast<void*>(_socket), 0, ZMQ_POLLIN, 0}};
                zmq::poll(&items[0], 1, std::chrono::milliseconds{timeout});

                if (items[0].revents & ZMQ_POLLIN) {
                    // Receive data
                    zmq::message_t              msg;
                    [[maybe_unused]] const bool ok = bool(_socket.recv(msg));

                    auto&  vec  = outputSpan[i];
                    size_t nels = msg.size() / sizeof(typename T::value_type);
                    vec.resize(nels);
                    std::copy(static_cast<typename T::value_type*>(msg.data()), static_cast<typename T::value_type*>(msg.data()) + nels, vec.begin());

                    npublished++;

                } else {
                    break;
                }
            }
        } else if constexpr (is_arithmetic_or_complex_v<T>) {

            while (true) {
                size_t room_in_span = nProcessOut - npublished;
                // std::cout << "1 nProcessOut: " << nProcessOut << " npublished: " << npublished << " room_in_span: " << room_in_span << std::endl;
                // std::cout << "1_pending_items: " << _pending_items.size() << std::endl;
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
                // std::cout << "2 nProcessOut: " << nProcessOut << " npublished: " << npublished << " room_in_span: " << room_in_span << std::endl;
                // std::cout << "2 _pending_items: " << _pending_items.size() << std::endl;


                zmq::pollitem_t items[] = {{static_cast<void*>(_socket), 0, ZMQ_POLLIN, 0}};
                zmq::poll(&items[0], 1, std::chrono::milliseconds{timeout});

                if (items[0].revents & ZMQ_POLLIN) {
                    // Receive data
                    zmq::message_t              msg;
                    [[maybe_unused]] const bool ok = bool(_socket.recv(msg));

                    auto&  vec  = outputSpan;
                    size_t nels = msg.size() / sizeof(T);
                    auto   n    = std::min(nels, room_in_span);
                    auto   rem  = nels - n;

                    // std::cout << "3 nProcessOut: " << nProcessOut << " npublished: " << npublished << " room_in_span: " << room_in_span << std::endl;
                    // std::cout << "3 _pending_items: " << _pending_items.size() << " rem: " << rem << " n: " << n << std::endl;

                    std::copy(static_cast<T*>(msg.data()), static_cast<T*>(msg.data()) + n, vec.begin() + npublished);
                    npublished += n;
                    if (rem) {
                        auto prev_pending_items_size = _pending_items.size();
                        _pending_items.resize(prev_pending_items_size + rem);
                        std::copy(static_cast<T*>(msg.data()) + n, static_cast<T*>(msg.data()) + n + rem, _pending_items.begin() + prev_pending_items_size);
                        break;
                    }

                } else {
                    break;
                }
            }
        }

        outputSpan.publish(npublished);

        return gr::work::Status::OK;
    }
};

} // namespace gr::zeromq

GR_REGISTER_BLOCK("ZmqPullSource", gr::zeromq::ZmqPullSource, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float>, std::vector<float>, std::vector<std::complex<float>> ])