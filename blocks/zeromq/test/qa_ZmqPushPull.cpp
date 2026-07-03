#include <boost/ut.hpp>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <format>
#include <expected>
#include <future>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/LifeCycle.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/zeromq/ZmqPubSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqReqSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqRepSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqSubSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>
#include <zmq.hpp>

using namespace boost::ut;

namespace {

gr::property_map make_props(std::initializer_list<std::pair<std::string_view, gr::pmt::Value>> init) {
    gr::property_map out;
    auto*            mr = out.get_allocator().resource();
    for (const auto& [key, value] : init) {
        out.emplace(gr::pmt::Value::Map::value_type{std::pmr::string(key.data(), key.size(), mr), value});
    }
    return out;
}

std::string endpoint_for(int offset) {
    const int base = 40000 + (getpid() % 1000);
    return std::format("tcp://127.0.0.1:{}", base + offset);
}

template <typename T>
std::vector<std::uint8_t> to_bytes(const std::vector<T>& payload) {
    std::vector<std::uint8_t> bytes(payload.size() * sizeof(T));
    if (!payload.empty()) {
        std::memcpy(bytes.data(), payload.data(), bytes.size());
    }
    return bytes;
}

std::thread spawn_push_sender(std::string endpoint, std::vector<std::uint8_t> bytes, std::chrono::milliseconds delay = std::chrono::milliseconds{100}) {
    return std::thread([endpoint = std::move(endpoint), bytes = std::vector<std::vector<std::uint8_t>>{std::move(bytes)}, delay] {
        std::this_thread::sleep_for(delay);
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::push};
        socket.connect(endpoint);

        for (const auto& message_bytes : bytes) {
            zmq::message_t msg(message_bytes.size());
            if (!message_bytes.empty()) {
                std::memcpy(msg.data(), message_bytes.data(), message_bytes.size());
            }
            [[maybe_unused]] const bool ok = bool(socket.send(msg, zmq::send_flags::none));
        }
    });
}

std::thread spawn_push_sender(std::string endpoint, std::vector<std::vector<std::uint8_t>> messages, std::chrono::milliseconds delay = std::chrono::milliseconds{100}) {
    return std::thread([endpoint = std::move(endpoint), messages = std::move(messages), delay] {
        std::this_thread::sleep_for(delay);
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::push};
        socket.connect(endpoint);

        for (const auto& message_bytes : messages) {
            zmq::message_t msg(message_bytes.size());
            if (!message_bytes.empty()) {
                std::memcpy(msg.data(), message_bytes.data(), message_bytes.size());
            }
            [[maybe_unused]] const bool ok = bool(socket.send(msg, zmq::send_flags::none));
        }
    });
}

std::future<std::vector<std::vector<std::uint8_t>>> spawn_pull_receiver(std::string endpoint, std::size_t min_messages, std::chrono::milliseconds timeout = std::chrono::milliseconds{2000}) {
    return std::async(std::launch::async, [endpoint = std::move(endpoint), min_messages, timeout] {
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::pull};
        socket.connect(endpoint);

        std::vector<std::vector<std::uint8_t>> messages;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (messages.size() < min_messages && std::chrono::steady_clock::now() < deadline) {
            zmq::pollitem_t items[] = {{static_cast<void*>(socket), 0, ZMQ_POLLIN, 0}};
            zmq::poll(&items[0], 1, std::chrono::milliseconds{25});
            if ((items[0].revents & ZMQ_POLLIN) == 0) {
                continue;
            }

            zmq::message_t msg;
            if (socket.recv(msg)) {
                const auto* first = static_cast<const std::uint8_t*>(msg.data());
                messages.emplace_back(first, first + msg.size());
            }
        }
        return messages;
    });
}

std::future<std::vector<std::vector<std::uint8_t>>> spawn_sub_receiver(std::string endpoint, std::string subscribe_key, std::chrono::milliseconds timeout = std::chrono::milliseconds{2000}) {
    return std::async(std::launch::async, [endpoint = std::move(endpoint), subscribe_key = std::move(subscribe_key), timeout] {
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::sub};
        socket.set(zmq::sockopt::subscribe, subscribe_key);
        socket.connect(endpoint);

        std::vector<std::vector<std::uint8_t>> frames;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            zmq::pollitem_t items[] = {{static_cast<void*>(socket), 0, ZMQ_POLLIN, 0}};
            zmq::poll(&items[0], 1, std::chrono::milliseconds{25});
            if ((items[0].revents & ZMQ_POLLIN) == 0) {
                continue;
            }

            while (true) {
                zmq::message_t msg;
                if (!socket.recv(msg)) {
                    break;
                }
                const auto* first = static_cast<const std::uint8_t*>(msg.data());
                frames.emplace_back(first, first + msg.size());
                if (!socket.get(zmq::sockopt::rcvmore)) {
                    return frames;
                }
            }
        }
        return frames;
    });
}

std::thread spawn_pub_sender(std::string endpoint, std::string topic, std::vector<std::uint8_t> payload, std::chrono::milliseconds delay = std::chrono::milliseconds{100}) {
    return std::thread([endpoint = std::move(endpoint), topic = std::move(topic), payload = std::move(payload), delay] {
        std::this_thread::sleep_for(delay);
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::pub};
        socket.connect(endpoint);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1000};
        while (std::chrono::steady_clock::now() < deadline) {
            if (!topic.empty()) {
                zmq::message_t key_msg(topic.size());
                std::memcpy(key_msg.data(), topic.data(), topic.size());
                [[maybe_unused]] const bool ok_key = bool(socket.send(key_msg, zmq::send_flags::sndmore));
            }

            zmq::message_t msg(payload.size());
            if (!payload.empty()) {
                std::memcpy(msg.data(), payload.data(), payload.size());
            }
            [[maybe_unused]] const bool ok_payload = bool(socket.send(msg, zmq::send_flags::none));
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
        }
    });
}

std::thread spawn_pub_sender(std::string endpoint, std::string topic, std::vector<std::vector<std::uint8_t>> payloads, std::chrono::milliseconds delay = std::chrono::milliseconds{100}) {
    return std::thread([endpoint = std::move(endpoint), topic = std::move(topic), payloads = std::move(payloads), delay] {
        std::this_thread::sleep_for(delay);
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::pub};
        socket.connect(endpoint);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1000};
        while (std::chrono::steady_clock::now() < deadline) {
            for (const auto& payload : payloads) {
                if (!topic.empty()) {
                    zmq::message_t key_msg(topic.size());
                    std::memcpy(key_msg.data(), topic.data(), topic.size());
                    [[maybe_unused]] const bool ok_key = bool(socket.send(key_msg, zmq::send_flags::sndmore));
                }

                zmq::message_t msg(payload.size());
                if (!payload.empty()) {
                    std::memcpy(msg.data(), payload.data(), payload.size());
                }
                [[maybe_unused]] const bool ok_payload = bool(socket.send(msg, zmq::send_flags::none));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
        }
    });
}

std::future<uint32_t> spawn_rep_responder(std::string endpoint, std::vector<std::uint8_t> payload, std::chrono::milliseconds delay = std::chrono::milliseconds{100}) {
    return std::async(std::launch::async, [endpoint = std::move(endpoint), payload = std::move(payload), delay] {
        std::this_thread::sleep_for(delay);
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::rep};
        socket.connect(endpoint);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{2000};
        uint32_t   request_count = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            zmq::pollitem_t items[] = {{static_cast<void*>(socket), 0, ZMQ_POLLIN, 0}};
            zmq::poll(&items[0], 1, std::chrono::milliseconds{25});
            if ((items[0].revents & ZMQ_POLLIN) == 0) {
                continue;
            }

            zmq::message_t request;
            if (!socket.recv(request)) {
                continue;
            }
            if (request.size() >= sizeof(uint32_t)) {
                std::memcpy(&request_count, request.data(), sizeof(uint32_t));
            }

            zmq::message_t reply(payload.size());
            if (!payload.empty()) {
                std::memcpy(reply.data(), payload.data(), payload.size());
            }
            [[maybe_unused]] const bool ok = bool(socket.send(reply, zmq::send_flags::none));
            return request_count;
        }
        return request_count;
    });
}

std::future<std::vector<std::vector<std::uint8_t>>> spawn_req_client(std::string endpoint, std::vector<uint32_t> request_counts, std::chrono::milliseconds delay = std::chrono::milliseconds{100}) {
    return std::async(std::launch::async, [endpoint = std::move(endpoint), request_counts = std::move(request_counts), delay] {
        std::this_thread::sleep_for(delay);
        zmq::context_t ctx{1};
        zmq::socket_t   socket{ctx, zmq::socket_type::req};
        socket.connect(endpoint);

        std::vector<std::vector<std::uint8_t>> replies;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{4000};
        for (const auto request_count : request_counts) {
            zmq::message_t request(sizeof(request_count));
            std::memcpy(request.data(), &request_count, sizeof(request_count));
            if (!socket.send(request, zmq::send_flags::none)) {
                break;
            }

            while (std::chrono::steady_clock::now() < deadline) {
                zmq::pollitem_t items[] = {{static_cast<void*>(socket), 0, ZMQ_POLLIN, 0}};
                zmq::poll(&items[0], 1, std::chrono::milliseconds{25});
                if ((items[0].revents & ZMQ_POLLIN) == 0) {
                    continue;
                }

                zmq::message_t reply;
                if (socket.recv(reply)) {
                    const auto* first = static_cast<const std::uint8_t*>(reply.data());
                    replies.emplace_back(first, first + reply.size());
                }
                break;
            }
        }
        return replies;
    });
}

template <typename SchedulerT>
bool run_with_timeout(SchedulerT& sched, std::chrono::milliseconds timeout) {
    std::mutex                          mu;
    std::condition_variable             cv;
    std::optional<std::expected<void, gr::Error>> result;
    bool                                done = false;

    std::thread runner([&] {
        auto res = sched.runAndWait();
        {
            std::lock_guard lk(mu);
            result = std::move(res);
            done = true;
        }
        cv.notify_one();
    });

    {
        std::unique_lock lk(mu);
        if (!cv.wait_for(lk, timeout, [&] { return done; })) {
            std::ignore = sched.changeStateTo(gr::lifecycle::State::REQUESTED_STOP);
            lk.unlock();
            if (runner.joinable()) {
                runner.join();
            }
            return false;
        }
    }

    if (runner.joinable()) {
        runner.join();
    }
    return result.has_value() && result->has_value();
}

template <typename SchedulerT, typename Predicate>
bool run_until_then_stop(SchedulerT& sched, Predicate&& done, std::chrono::milliseconds timeout) {
    std::mutex                          mu;
    std::condition_variable             cv;
    std::optional<std::expected<void, gr::Error>> result;
    bool                                scheduler_done = false;

    std::thread runner([&] {
        auto res = sched.runAndWait();
        {
            std::lock_guard lk(mu);
            result = std::move(res);
            scheduler_done = true;
        }
        cv.notify_one();
    });

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool predicate_done = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            predicate_done = true;
            break;
        }

        {
            std::lock_guard lk(mu);
            if (scheduler_done) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    std::ignore = sched.changeStateTo(gr::lifecycle::State::REQUESTED_STOP);

    if (runner.joinable()) {
        runner.join();
    }

    return predicate_done && result.has_value() && result->has_value();
}

template <typename T>
struct DelayedCountingSource : gr::Block<DelayedCountingSource<T>> {
    gr::PortOut<T> out;
    gr::Size_t n_samples_max = 0;
    gr::Size_t count = 0;
    gr::Size_t startup_delay_ms = 50;
    bool       started = false;

    GR_MAKE_REFLECTABLE(DelayedCountingSource, out, n_samples_max, count, startup_delay_ms);

    [[nodiscard]] T processOne() noexcept {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        ++count;
        if (n_samples_max > 0 && count >= n_samples_max) {
            this->requestStop();
        }
        return static_cast<T>(count);
    }
};

template <typename T>
struct ConstantSource : gr::Block<ConstantSource<T>> {
    gr::PortOut<T> out;
    T              value{};
    bool           emitted = false;
    gr::Size_t     startup_delay_ms = 50;

    GR_MAKE_REFLECTABLE(ConstantSource, out, value, startup_delay_ms);

    [[nodiscard]] T processOne() {
        if (emitted) {
            this->requestStop();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        emitted = true;
        return value;
    }
};

template <typename T>
struct VectorSink : gr::Block<VectorSink<T>> {
    gr::PortIn<std::vector<T>> in;
    gr::Size_t n_samples_max = 0;
    gr::Size_t count = 0;
    gr::Size_t last_size = 0;

    GR_MAKE_REFLECTABLE(VectorSink, in, n_samples_max, count, last_size);

    void processOne(const std::vector<T>& v) noexcept {
        ++count;
        last_size = v.size();
        if (n_samples_max > 0 && count >= n_samples_max) {
            this->requestStop();
        }
    }
};

struct PmtSink : gr::Block<PmtSink> {
    gr::PortIn<gr::pmt::Value> in;
    gr::Size_t n_samples_max = 0;
    gr::Size_t count = 0;

    GR_MAKE_REFLECTABLE(PmtSink, in, n_samples_max, count);

    void processOne(const gr::pmt::Value&) noexcept {
        ++count;
        if (n_samples_max > 0 && count >= n_samples_max) {
            this->requestStop();
        }
    }
};

} // namespace

const suite ZmqPushPullTests = [] {
    "Pull source receives scalar raw payload"_test = [] {
        gr::Graph fg;
        using T = float;
        constexpr gr::Size_t n_samples = 16;
        const auto endpoint = endpoint_for(0);

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(n_samples)},
        }));

        expect(fg.connect<"out", "in">(pull, sink) .has_value());

        auto sender = spawn_push_sender(endpoint, to_bytes(std::vector<T>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f}));

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= n_samples; }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, n_samples));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Pull source receives vector<complex<float>> messages"_test = [] {
        gr::Graph fg;
        using T = std::complex<float>;
        constexpr gr::Size_t n_messages = 4;
        constexpr gr::Size_t payload_len = 16;
        const auto endpoint = endpoint_for(1);

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<std::vector<T>>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<VectorSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(n_messages)},
        }));

        expect(fg.connect<"out", "in">(pull, sink) .has_value());

        std::vector<T> payload(payload_len);
        for (gr::Size_t i = 0; i < payload_len; ++i) {
            payload[i] = T{static_cast<float>(i + 1), static_cast<float>(i + 2)};
        }
        std::vector<std::vector<std::uint8_t>> messages;
        for (gr::Size_t i = 0; i < n_messages; ++i) {
            messages.push_back(to_bytes(payload));
        }
        auto sender = spawn_push_sender(endpoint, std::move(messages));

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= n_messages; }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, n_messages));
        expect(eq(sink.last_size, payload_len));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Pull source receives pmt::Value messages"_test = [] {
        gr::Graph fg;
        constexpr gr::Size_t n_messages = 4;
        const auto endpoint = endpoint_for(2);

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<gr::pmt::Value>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<PmtSink>(make_props({
            {"n_samples_max", gr::pmt::Value(n_messages)},
        }));

        expect(fg.connect<"out", "in">(pull, sink) .has_value());

        std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f};
        auto serialized = legacy_pmt::serialize_to_legacy(gr::pmt::Value(gr::Tensor<float>(gr::data_from, vec)));
        std::vector<std::vector<std::uint8_t>> messages(n_messages, serialized);
        auto sender = spawn_push_sender(endpoint, std::move(messages));

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= n_messages; }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, n_messages));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Pull source times out without inbound data"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(3);

        auto& source = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<float>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<float>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(1))},
        }));

        expect(fg.connect<"out", "in">(source, sink) .has_value());

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(!run_with_timeout(sched, std::chrono::milliseconds(300)));
    };

    "Push sink sends scalar stream payload"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(4);
        auto receiver = spawn_pull_receiver(endpoint, 1);

        auto& source = fg.emplaceBlock<DelayedCountingSource<float>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(0))},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(50))},
        }));
        auto& push = fg.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<float>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        }));

        expect(fg.connect<"out", "in">(source, push) .has_value());

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return receiver.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(2000)));
        expect(source.count >= static_cast<gr::Size_t>(1));

        auto messages = receiver.get();
        expect(eq(messages.size(), static_cast<std::size_t>(1)));
        if (!messages.empty()) {
            expect(messages.front().size() >= sizeof(float));
            expect(eq(messages.front().size() % sizeof(float), static_cast<std::size_t>(0)));
        }
    };

    "Pull source flushes oversized raw message across spans"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(5);

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(16))},
        }));

        expect(fg.connect<"out", "in">(pull, sink) .has_value());

        auto sender = spawn_push_sender(endpoint, to_bytes(std::vector<T>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f}));

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= static_cast<gr::Size_t>(16); }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, static_cast<gr::Size_t>(16)));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Pull source rejects invalid raw payload size"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(6);

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(1))},
        }));

        expect(fg.connect<"out", "in">(pull, sink) .has_value());

        auto sender = spawn_push_sender(endpoint, std::vector<std::uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_with_timeout(sched, std::chrono::milliseconds(10000)));
        expect(sched.state() == gr::lifecycle::State::ERROR);
        expect(eq(sink.count, static_cast<gr::Size_t>(0)));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Pub sink emits keyed multipart raw payload"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(7);
        const std::string key = "telemetry.";

        auto receiver = spawn_sub_receiver(endpoint, key);

        auto& source = fg.emplaceBlock<DelayedCountingSource<float>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(3))},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(100))},
        }));
        auto& pub = fg.emplaceBlock<gr::incubator::zeromq::ZmqPubSink<float>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"key", gr::pmt::Value(key)},
        }));

        expect(fg.connect<"out", "in">(source, pub).has_value());

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        const bool completed = run_until_then_stop(sched, [&] { return receiver.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(2000));
        expect(completed);
        if (completed) {
            auto frames = receiver.get();
            expect(eq(frames.size(), static_cast<std::size_t>(2)));
            if (frames.size() == 2) {
                expect(eq(std::string_view(reinterpret_cast<const char*>(frames[0].data()), frames[0].size()), key));
                expect(eq(frames[1].size() % sizeof(float), static_cast<std::size_t>(0)));
            }
        }

        expect(source.count >= static_cast<gr::Size_t>(1));
    };

    "Sub source receives keyed raw payload"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(8);
        const std::string key = "telemetry.";

        auto& sub = fg.emplaceBlock<gr::incubator::zeromq::ZmqSubSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"key", gr::pmt::Value(key)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(3))},
        }));

        expect(fg.connect<"out", "in">(sub, sink).has_value());

        auto sender = spawn_pub_sender(endpoint, key, to_bytes(std::vector<T>{1.f, 2.f, 3.f}), std::chrono::milliseconds{100});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= static_cast<gr::Size_t>(3); }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, static_cast<gr::Size_t>(3)));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Sub source subscribes to topic prefix"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(9);
        const std::string subscribe_key = "telemetry.";
        const std::string published_key = "telemetry.stream";

        auto& sub = fg.emplaceBlock<gr::incubator::zeromq::ZmqSubSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"key", gr::pmt::Value(subscribe_key)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(2))},
        }));

        expect(fg.connect<"out", "in">(sub, sink).has_value());

        auto sender = spawn_pub_sender(endpoint, published_key, to_bytes(std::vector<T>{4.f, 5.f}), std::chrono::milliseconds{100});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= static_cast<gr::Size_t>(2); }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, static_cast<gr::Size_t>(2)));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Sub source rejects wrong topic"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(10);

        auto& sub = fg.emplaceBlock<gr::incubator::zeromq::ZmqSubSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"key", gr::pmt::Value(std::string("telemetry."))},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(1))},
        }));

        expect(fg.connect<"out", "in">(sub, sink).has_value());

        auto sender = spawn_pub_sender(endpoint, std::string("wrong."), to_bytes(std::vector<T>{6.f}), std::chrono::milliseconds{100});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(!run_with_timeout(sched, std::chrono::milliseconds(300)));
        expect(eq(sink.count, static_cast<gr::Size_t>(0)));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Sub source with empty key receives all topics"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(11);

        auto& sub = fg.emplaceBlock<gr::incubator::zeromq::ZmqSubSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"key", gr::pmt::Value(std::string())},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(2))},
        }));

        expect(fg.connect<"out", "in">(sub, sink).has_value());

        auto sender = spawn_pub_sender(endpoint, std::string("telemetry.stream"), to_bytes(std::vector<T>{7.f, 8.f}), std::chrono::milliseconds{100});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= static_cast<gr::Size_t>(2); }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, static_cast<gr::Size_t>(2)));

        if (sender.joinable()) {
            sender.join();
        }
    };

    "Req source receives raw payloads"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(14);

        auto& source = fg.emplaceBlock<gr::incubator::zeromq::ZmqReqSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(100)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(5))},
        }));

        expect(fg.connect<"out", "in">(source, sink).has_value());

        auto responder = spawn_rep_responder(endpoint, to_bytes(std::vector<T>{1.f, 2.f, 3.f, 4.f, 5.f}), std::chrono::milliseconds{10});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= static_cast<gr::Size_t>(5); }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, static_cast<gr::Size_t>(5)));

        expect(responder.valid());
        if (responder.valid()) {
            expect(responder.get() >= 1U);
        }
    };

    "Rep sink honors smaller and equal request counts"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(15);

        auto& source = fg.emplaceBlock<DelayedCountingSource<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(5))},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(50))},
        }));
        auto& rep = fg.emplaceBlock<gr::incubator::zeromq::ZmqRepSink<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(100)},
            {"bind", gr::pmt::Value(true)},
        }));

        expect(fg.connect<"out", "in">(source, rep).has_value());

        auto client = spawn_req_client(endpoint, std::vector<uint32_t>{2U, 3U}, std::chrono::milliseconds{10});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return client.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(3000)));

        auto replies = client.get();
        expect(eq(replies.size(), static_cast<std::size_t>(2)));
        if (replies.size() == 2) {
            expect(eq(replies[0].size(), 2U * sizeof(T)));
            expect(eq(replies[1].size(), 3U * sizeof(T)));
            const auto* first = reinterpret_cast<const T*>(replies[0].data());
            const auto* second = reinterpret_cast<const T*>(replies[1].data());
            expect(eq(first[0], 1.f));
            expect(eq(first[1], 2.f));
            expect(eq(second[0], 3.f));
            expect(eq(second[1], 4.f));
            expect(eq(second[2], 5.f));
        }
    };

    "Rep sink caps larger request counts"_test = [] {
        gr::Graph fg;
        using T = float;
        const auto endpoint = endpoint_for(16);

        auto& source = fg.emplaceBlock<DelayedCountingSource<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(5))},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(50))},
        }));
        auto& rep = fg.emplaceBlock<gr::incubator::zeromq::ZmqRepSink<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(100)},
            {"bind", gr::pmt::Value(true)},
        }));

        expect(fg.connect<"out", "in">(source, rep).has_value());

        auto client = spawn_req_client(endpoint, std::vector<uint32_t>{7U}, std::chrono::milliseconds{10});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return client.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(3000)));

        auto replies = client.get();
        expect(eq(replies.size(), static_cast<std::size_t>(1)));
        if (replies.size() == 1) {
            expect(eq(replies[0].size(), 5U * sizeof(T)));
        }
    };

    "Req/Rep PMT payloads use legacy serialization"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(17);

        auto& source = fg.emplaceBlock<ConstantSource<gr::pmt::Value>>();
        source.value = gr::pmt::Value(gr::Tensor<float>(gr::data_from, std::vector<float>{1.f, 2.f, 3.f, 4.f}));
        auto& rep = fg.emplaceBlock<gr::incubator::zeromq::ZmqRepSink<gr::pmt::Value>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(100)},
            {"bind", gr::pmt::Value(true)},
        }));

        expect(fg.connect<"out", "in">(source, rep).has_value());

        auto expected = legacy_pmt::serialize_to_legacy(gr::pmt::Value(gr::Tensor<float>(gr::data_from, std::vector<float>{1.f, 2.f, 3.f, 4.f})));
        auto client = spawn_req_client(endpoint, std::vector<uint32_t>{1U}, std::chrono::milliseconds{10});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return client.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(3000)));

        auto replies = client.get();
        expect(eq(replies.size(), static_cast<std::size_t>(1)));
        if (replies.size() == 1) {
            expect(replies[0] == expected);
        }
    };

    "Req source receives legacy PMT payload"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(18);

        auto& source = fg.emplaceBlock<gr::incubator::zeromq::ZmqReqSource<gr::pmt::Value>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(100)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& sink = fg.emplaceBlock<PmtSink>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(1))},
        }));

        expect(fg.connect<"out", "in">(source, sink).has_value());

        auto payload = legacy_pmt::serialize_to_legacy(gr::pmt::Value(gr::Tensor<float>(gr::data_from, std::vector<float>{5.f, 6.f, 7.f, 8.f})));
        auto responder = spawn_rep_responder(endpoint, std::move(payload), std::chrono::milliseconds{10});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= static_cast<gr::Size_t>(1); }, std::chrono::milliseconds(3000)));
        expect(eq(sink.count, static_cast<gr::Size_t>(1)));

        expect(responder.valid());
        if (responder.valid()) {
            expect(responder.get() >= 1U);
        }
    };

    "Pub/Sub PMT payload uses legacy serialization"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(12);
        const std::string key = "pmt.";
        auto receiver = spawn_sub_receiver(endpoint, key);

        auto& source = fg.emplaceBlock<ConstantSource<gr::pmt::Value>>();
        source.value = gr::pmt::Value(gr::Tensor<float>(gr::data_from, std::vector<float>{1.f, 2.f, 3.f, 4.f}));
        auto& pub = fg.emplaceBlock<gr::incubator::zeromq::ZmqPubSink<gr::pmt::Value>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"key", gr::pmt::Value(key)},
        }));

        expect(fg.connect<"out", "in">(source, pub).has_value());

        const auto expected = legacy_pmt::serialize_to_legacy(source.value);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        const bool completed = run_until_then_stop(sched, [&] { return receiver.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(2000));
        expect(completed);
        if (completed) {
            auto frames = receiver.get();
            expect(eq(frames.size(), static_cast<std::size_t>(2)));
            if (frames.size() == 2) {
                expect(eq(std::string_view(reinterpret_cast<const char*>(frames[0].data()), frames[0].size()), key));
                expect(frames[1] == expected);
            }
        }
    };

    "Sub source receives legacy PMT payload"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(13);
        const std::string key = "pmt.";

        auto& sub = fg.emplaceBlock<gr::incubator::zeromq::ZmqSubSource<gr::pmt::Value>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"key", gr::pmt::Value(key)},
        }));
        auto& sink = fg.emplaceBlock<PmtSink>(make_props({
            {"n_samples_max", gr::pmt::Value(static_cast<gr::Size_t>(1))},
        }));

        expect(fg.connect<"out", "in">(sub, sink).has_value());

        auto payload = legacy_pmt::serialize_to_legacy(gr::pmt::Value(gr::Tensor<float>(gr::data_from, std::vector<float>{1.f, 2.f, 3.f, 4.f})));
        auto sender = spawn_pub_sender(endpoint, key, std::move(payload), std::chrono::milliseconds{100});

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.count >= static_cast<gr::Size_t>(1); }, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, static_cast<gr::Size_t>(1)));

        if (sender.joinable()) {
            sender.join();
        }
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
