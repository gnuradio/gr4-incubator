#pragma once

#include "detail/ZmqCommon.hpp"
#include "detail/ZmqTagHeaders.hpp"
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

    [[maybe_unused]] std::vector<T>                            _pending_items;
    [[maybe_unused]] std::vector<detail::ZmqTagHeaderRecord> _pending_tags;
    bool                                                      _req_pending = false;

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

        auto publish_pending_tags = [&outputSpan](std::size_t base_offset, std::size_t consumed, std::vector<detail::ZmqTagHeaderRecord>& tags) {
            std::vector<detail::ZmqTagHeaderRecord> remaining;
            remaining.reserve(tags.size());
            for (auto& tag : tags) {
                if (tag.offset < consumed) {
                    outputSpan.publishTag(detail::tag_map_from_record(tag), base_offset + static_cast<std::size_t>(tag.offset));
                } else {
                    tag.offset -= consumed;
                    remaining.push_back(std::move(tag));
                }
            }
            tags = std::move(remaining);
        };

        if constexpr (is_arithmetic_or_complex_v<T>) {
            while (true) {
                if (!_pending_items.empty()) {
                    const auto n = std::min(nProcessOut - npublished, _pending_items.size());
                    std::copy(_pending_items.begin(), _pending_items.begin() + static_cast<std::ptrdiff_t>(n), outputSpan.begin() + npublished);
                    publish_pending_tags(npublished, n, _pending_tags);
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

                std::uint64_t header_offset = 0;
                std::vector<detail::ZmqTagHeaderRecord> tags;
                std::size_t consumed_bytes = 0;
                if (pass_tags) {
                    consumed_bytes = detail::parse_tag_header(static_cast<const std::uint8_t*>(msg.data()), msg.size(), header_offset, tags);
                    for (auto& tag : tags) {
                        if (tag.offset >= header_offset) {
                            tag.offset -= header_offset;
                        }
                    }
                }
                const auto payload_size = msg.size() - consumed_bytes;
                detail::ZmqSocketTransport::require_multiple_of(payload_size, sizeof(T), "ZmqReqSource scalar payload");
                const std::size_t nels = payload_size / sizeof(T);
                const std::size_t n    = std::min(nels, nProcessOut - npublished);
                const auto* payload = static_cast<const std::uint8_t*>(msg.data()) + consumed_bytes;
                const auto* typed_payload = reinterpret_cast<const T*>(payload);
                std::copy(typed_payload, typed_payload + static_cast<std::ptrdiff_t>(n), outputSpan.begin() + npublished);
                publish_pending_tags(npublished, n, _pending_tags);
                publish_pending_tags(npublished, n, tags);
                npublished += n;

                if (nels > n) {
                    _pending_items.resize(nels - n);
                    std::copy(typed_payload + static_cast<std::ptrdiff_t>(n),
                              typed_payload + static_cast<std::ptrdiff_t>(nels),
                              _pending_items.begin());
                    _pending_tags = std::move(tags);
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

                auto& vec = outputSpan[npublished];
                std::uint64_t header_offset = 0;
                std::vector<detail::ZmqTagHeaderRecord> tags;
                std::size_t consumed_bytes = 0;
                if (pass_tags) {
                    consumed_bytes = detail::parse_tag_header(static_cast<const std::uint8_t*>(msg.data()), msg.size(), header_offset, tags);
                    for (auto& tag : tags) {
                        if (tag.offset >= header_offset) {
                            tag.offset -= header_offset;
                        }
                    }
                }
                const auto payload_size = msg.size() - consumed_bytes;
                detail::ZmqSocketTransport::require_multiple_of(payload_size, sizeof(typename T::value_type), "ZmqReqSource vector payload");
                const auto* payload = static_cast<const std::uint8_t*>(msg.data()) + consumed_bytes;
                const std::size_t nels = payload_size / sizeof(typename T::value_type);
                vec.resize(nels);
                const auto* typed_payload = reinterpret_cast<const typename T::value_type*>(payload);
                std::copy(typed_payload,
                          typed_payload + static_cast<std::ptrdiff_t>(nels),
                          vec.begin());
                if (pass_tags) {
                    for (const auto& tag : tags) {
                        outputSpan.publishTag(detail::tag_map_from_record(tag), npublished);
                    }
                }
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
                    std::uint64_t header_offset = 0;
                    std::vector<detail::ZmqTagHeaderRecord> tags;
                    std::size_t consumed_bytes = 0;
                    if (pass_tags) {
                        consumed_bytes = detail::parse_tag_header(static_cast<const std::uint8_t*>(msg.data()), msg.size(), header_offset, tags);
                        for (auto& tag : tags) {
                            if (tag.offset >= header_offset) {
                                tag.offset -= header_offset;
                            }
                        }
                    }
                    outputSpan[npublished] = legacy_pmt::deserialize_from_legacy(static_cast<const uint8_t*>(msg.data()) + consumed_bytes, msg.size() - consumed_bytes);
                    if (pass_tags) {
                        for (const auto& tag : tags) {
                            outputSpan.publishTag(detail::tag_map_from_record(tag), npublished);
                        }
                    }
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
