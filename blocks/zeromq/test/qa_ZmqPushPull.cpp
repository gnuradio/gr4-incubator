#include <boost/ut.hpp>

#include <chrono>
#include <cstring>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <format>
#include <future>
#include <memory_resource>
#include <string>
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
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
