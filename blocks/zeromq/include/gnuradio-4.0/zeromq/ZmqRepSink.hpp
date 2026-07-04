#pragma once

#include "detail/ZmqCommon.hpp"
#include "detail/ZmqTagHeaders.hpp"
#include "trait_helpers.hpp"

#include <algorithm>
#include <cstring>
#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <gnuradio-4.0/meta/reflection.hpp>
#include <vector>

namespace gr::incubator::zeromq {

template<typename T>
concept ZmqRepSinkAcceptableTypes = std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>> || std::is_same_v<T, std::complex<typename T::value_type>> || std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same_v<T, gr::pmt::Value>;

template<ZmqRepSinkAcceptableTypes T>
class ZmqRepSink : public gr::Block<ZmqRepSink<T>> {
public:
    using Description = Doc<R""(
@brief ZMQ REP Sink.

This block receives stream items and replies to REQ requests using a REP socket.

)"">;

public:
    gr::PortIn<T>  in;
    std::string    endpoint = "tcp://*:5555";
    int            timeout  = 100;
    bool           bind     = true;
    bool           pass_tags = false;
    int            linger   = 1000;
    int            hwm      = -1;

    detail::ZmqSocketTransport _transport{zmq::socket_type::rep};

    [[maybe_unused]] std::vector<T>                            _pending_items;
    [[maybe_unused]] std::vector<detail::ZmqTagHeaderRecord> _pending_tags;

    GR_MAKE_REFLECTABLE(ZmqRepSink, in, endpoint, timeout, bind, pass_tags, linger, hwm);

    void start() {
        _transport.open(endpoint, bind, linger, hwm, true);
    }

    [[nodiscard]] std::string last_endpoint() const { return _transport.last_endpoint(); }

    static std::size_t parse_request_count(const zmq::message_t& request) {
        uint32_t n = 1;
        if (request.size() >= sizeof(uint32_t)) {
            std::memcpy(&n, request.data(), sizeof(uint32_t));
        }
        return static_cast<std::size_t>(n);
    }

    [[nodiscard]] work::Status processBulk(InputSpanLike auto& inData) {
        if (inData.size() == 0 && _pending_items.empty()) {
            return gr::work::Status::OK;
        }

        if (inData.size() > 0) {
            const auto old_size = _pending_items.size();
            _pending_items.resize(old_size + inData.size());
            std::copy(inData.begin(), inData.end(), _pending_items.begin() + static_cast<std::ptrdiff_t>(old_size));
            if (pass_tags) {
                for (const auto& [relIndex, tagMapRef] : inData.tags()) {
                    if (relIndex < 0) {
                        continue;
                    }
                    auto rec = detail::tag_record_from_property_map(tagMapRef.get());
                    if (!rec.has_value()) {
                        continue;
                    }
                    rec->offset = old_size + static_cast<std::uint64_t>(relIndex);
                    _pending_tags.push_back(std::move(*rec));
                }
            }
            std::ignore = inData.consume(inData.size());
        }

        auto take_tags = [&](std::size_t nsend) {
            std::vector<detail::ZmqTagHeaderRecord> sent_tags;
            std::vector<detail::ZmqTagHeaderRecord> remaining;
            for (auto& tag : _pending_tags) {
                if (tag.offset < nsend) {
                    sent_tags.push_back(tag);
                } else {
                    tag.offset -= nsend;
                    remaining.push_back(std::move(tag));
                }
            }
            _pending_tags = std::move(remaining);
            return sent_tags;
        };

        if constexpr (is_arithmetic_or_complex_v<T>) {
            while (!_pending_items.empty()) {
                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t request;
                if (!bool(_transport.socket().recv(request))) {
                    break;
                }

                const std::size_t requested = parse_request_count(request);
                const std::size_t nsend     = std::min(requested, _pending_items.size());
                if (nsend == 0) {
                    break;
                }

                const auto tags = pass_tags ? take_tags(nsend) : std::vector<detail::ZmqTagHeaderRecord>{};
                const auto header = pass_tags ? detail::serialize_tag_header(0, tags) : std::vector<std::uint8_t>{};
                zmq::message_t reply(header.size() + nsend * sizeof(T));
                if (!header.empty()) {
                    std::memcpy(reply.data(), header.data(), header.size());
                }
                std::memcpy(static_cast<std::uint8_t*>(reply.data()) + header.size(), _pending_items.data(), nsend * sizeof(T));
                if (!bool(_transport.socket().send(reply, zmq::send_flags::dontwait))) {
                    break;
                }
                _pending_items.erase(_pending_items.begin(), _pending_items.begin() + static_cast<std::ptrdiff_t>(nsend));
            }
        } else if constexpr (is_vector_of_arithmetic_or_complex_v<T>) {
            while (!_pending_items.empty()) {
                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t request;
                if (!bool(_transport.socket().recv(request))) {
                    break;
                }

                auto& vec = _pending_items.front();
                const auto tags = pass_tags ? take_tags(1) : std::vector<detail::ZmqTagHeaderRecord>{};
                const auto header = pass_tags ? detail::serialize_tag_header(0, tags) : std::vector<std::uint8_t>{};
                zmq::message_t reply(header.size() + vec.size() * sizeof(typename T::value_type));
                if (!header.empty()) {
                    std::memcpy(reply.data(), header.data(), header.size());
                }
                if (!vec.empty()) {
                    std::memcpy(static_cast<std::uint8_t*>(reply.data()) + header.size(), vec.data(), vec.size() * sizeof(typename T::value_type));
                }
                if (!bool(_transport.socket().send(reply, zmq::send_flags::dontwait))) {
                    break;
                }
                _pending_items.erase(_pending_items.begin());
            }
        } else if constexpr (std::is_same_v<T, gr::pmt::Value>) {
            while (!_pending_items.empty()) {
                if (!_transport.wait_readable(timeout)) {
                    break;
                }

                zmq::message_t request;
                if (!bool(_transport.socket().recv(request))) {
                    break;
                }

                const auto tags = pass_tags ? take_tags(1) : std::vector<detail::ZmqTagHeaderRecord>{};
                const auto header = pass_tags ? detail::serialize_tag_header(0, tags) : std::vector<std::uint8_t>{};
                std::vector<uint8_t> serialized = legacy_pmt::serialize_to_legacy(_pending_items.front());
                zmq::message_t reply(header.size() + serialized.size());
                if (!header.empty()) {
                    std::memcpy(reply.data(), header.data(), header.size());
                }
                if (!serialized.empty()) {
                    std::memcpy(static_cast<std::uint8_t*>(reply.data()) + header.size(), serialized.data(), serialized.size());
                }
                if (!bool(_transport.socket().send(reply, zmq::send_flags::dontwait))) {
                    break;
                }
                _pending_items.erase(_pending_items.begin());
            }
        }

        return gr::work::Status::OK;
    }
};

} // namespace gr::incubator::zeromq

GR_REGISTER_BLOCK("gr::incubator::zeromq::ZmqRepSink", gr::incubator::zeromq::ZmqRepSink, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float>, std::vector<float>, std::vector<std::complex<float>>, gr::pmt::Value ])
