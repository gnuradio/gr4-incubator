#pragma once

#include "detail/ZmqCommon.hpp"
#include "detail/ZmqTagHeaders.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/meta/reflection.hpp>
#include "trait_helpers.hpp"
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <vector>


namespace gr::incubator::zeromq {

template<typename T>
concept ZmqPullSourceAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same_v<T, gr::pmt::Value>;

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
    bool           pass_tags = false;
    int            linger   = 1000;
    int            hwm      = -1;

    detail::ZmqSocketTransport _transport{zmq::socket_type::pull};

    [[maybe_unused]] std::vector<T> _pending_items;
    [[maybe_unused]] std::vector<detail::ZmqTagHeaderRecord> _pending_tags;

    GR_MAKE_REFLECTABLE(ZmqPullSource, out, endpoint, timeout, bind, pass_tags, linger, hwm);

    void start() {
        _transport.open(endpoint, bind, linger, hwm, false);
    }

    [[nodiscard]] std::string last_endpoint() const { return _transport.last_endpoint(); }

    [[nodiscard]] work::Status processBulk(OutputSpanLike auto& outputSpan) {
        const std::size_t nProcessOut = outputSpan.size();

        size_t npublished = 0;

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

        if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            for (std::size_t i = 0; i < nProcessOut; ++i) {
                if (!_pending_items.empty()) {
                    auto& vec = outputSpan[npublished];
                    vec = _pending_items.front();
                    publish_pending_tags(npublished, 1, _pending_tags);
                    ++npublished;
                    _pending_items.erase(_pending_items.begin());
                    if (npublished >= nProcessOut) {
                        break;
                    }
                }
                if (_transport.wait_readable(timeout)) {
                    // Receive data
                    zmq::message_t              msg;
                    [[maybe_unused]] const bool ok = bool(_transport.socket().recv(msg));
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
                    const auto* payload = static_cast<const std::uint8_t*>(msg.data()) + consumed_bytes;
                    const auto payload_size = msg.size() - consumed_bytes;
                    if (!detail::ZmqSocketTransport::is_multiple_of(payload_size, sizeof(typename T::value_type))) {
                        outputSpan.publish(npublished);
                        return gr::work::Status::ERROR;
                    }

                    auto&  vec  = outputSpan[npublished];
                    size_t nels = payload_size / sizeof(typename T::value_type);
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
                    publish_pending_tags(npublished, n, _pending_tags);
                    npublished += n;

                    _pending_items = std::vector<T>(_pending_items.begin() + n, _pending_items.end());
                }

                room_in_span = nProcessOut - npublished;
                if (!room_in_span) {
                    break;
                }
                if (_transport.wait_readable(timeout)) {
                    // Receive data
                    zmq::message_t              msg;
                    [[maybe_unused]] const bool ok = bool(_transport.socket().recv(msg));
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
                    const auto* payload = static_cast<const std::uint8_t*>(msg.data()) + consumed_bytes;
                    const auto payload_size = msg.size() - consumed_bytes;
                    if (!detail::ZmqSocketTransport::is_multiple_of(payload_size, sizeof(T))) {
                        outputSpan.publish(npublished);
                        return gr::work::Status::ERROR;
                    }

                    auto&  vec  = outputSpan;
                    size_t nels = payload_size / sizeof(T);
                    auto   n    = std::min(nels, room_in_span);
                    auto   rem  = nels - n;

                    // std::cout << "3 nProcessOut: " << nProcessOut << " npublished: " << npublished << " room_in_span: " << room_in_span << std::endl;
                    // std::cout << "3 _pending_items: " << _pending_items.size() << " rem: " << rem << " n: " << n << std::endl;

                    const auto* typed_payload = reinterpret_cast<const T*>(payload);
                    std::copy(typed_payload, typed_payload + static_cast<std::ptrdiff_t>(n), vec.begin() + npublished);
                    publish_pending_tags(npublished, n, _pending_tags);
                    publish_pending_tags(npublished, n, tags);
                    npublished += n;
                    if (rem) {
                        auto prev_pending_items_size = _pending_items.size();
                        _pending_items.resize(prev_pending_items_size + rem);
                        std::copy(typed_payload + static_cast<std::ptrdiff_t>(n),
                                  typed_payload + static_cast<std::ptrdiff_t>(n + rem),
                                  _pending_items.begin() + prev_pending_items_size);
                        _pending_tags = std::move(tags);
                        break;
                    }

                } else {
                    break;
                }
            }
        } else if constexpr (std::is_same_v<T, gr::pmt::Value>) {
            for (std::size_t i = 0; i < nProcessOut; ++i) {
                if (_transport.wait_readable(timeout)) {
                try {
                    zmq::message_t msg;
                    [[maybe_unused]] const bool ok = bool(_transport.socket().recv(msg));
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
                    outputSpan[i] = legacy_pmt::deserialize_from_legacy(static_cast<const uint8_t*>(msg.data()) + consumed_bytes, msg.size() - consumed_bytes);
                    if (pass_tags) {
                        for (const auto& tag : tags) {
                            outputSpan.publishTag(detail::tag_map_from_record(tag), npublished);
                        }
                    }
                    npublished++;
                } catch (...) {
                    outputSpan.publish(npublished);
                        return gr::work::Status::ERROR;
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

} // namespace gr::incubator::zeromq

GR_REGISTER_BLOCK("gr::incubator::zeromq::ZmqPullSource", gr::incubator::zeromq::ZmqPullSource, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float>, std::vector<float>, std::vector<std::complex<float>>, gr::pmt::Value ])
