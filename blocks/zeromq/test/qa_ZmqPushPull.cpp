#include <boost/ut.hpp>

#include <chrono>
#include <complex>
#include <cstdint>
#include <format>
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
#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>

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
struct VectorSource : gr::Block<VectorSource<T>> {
    gr::PortOut<std::vector<T>> out;
    gr::Size_t n_samples_max = 0;
    gr::Size_t count = 0;
    gr::Size_t payload_len = 8;
    gr::Size_t startup_delay_ms = 50;
    bool       started = false;

    GR_MAKE_REFLECTABLE(VectorSource, out, n_samples_max, count, payload_len, startup_delay_ms);

    std::vector<T> payload;

    void start() {
        payload.resize(payload_len);
        for (gr::Size_t i = 0; i < payload_len; ++i) {
            payload[i] = static_cast<T>(i + 1);
        }
    }

    [[nodiscard]] std::vector<T> processOne() noexcept {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        ++count;
        if (n_samples_max > 0 && count >= n_samples_max) {
            this->requestStop();
        }
        return payload;
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

struct PmtSource : gr::Block<PmtSource> {
    gr::PortOut<gr::pmt::Value> out;
    gr::Size_t n_samples_max = 0;
    gr::Size_t count = 0;
    gr::Size_t startup_delay_ms = 50;
    bool       started = false;

    GR_MAKE_REFLECTABLE(PmtSource, out, n_samples_max, count, startup_delay_ms);

    gr::pmt::Value payload;

    void start() {
        std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f};
        payload = gr::pmt::Value(gr::Tensor<float>(gr::data_from, vec));
    }

    [[nodiscard]] gr::pmt::Value processOne() noexcept {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        ++count;
        if (n_samples_max > 0 && count >= n_samples_max) {
            this->requestStop();
        }
        return payload;
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
    "Loopback complex<float>"_test = [] {
        gr::Graph fg;
        using T = std::complex<float>;
        constexpr gr::Size_t n_samples = 256;
        const auto endpoint = endpoint_for(0);

        auto& source = fg.emplaceBlock<DelayedCountingSource<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(n_samples)},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(500))},
        }));
        auto& push = fg.emplaceBlock<gr::zeromq::ZmqPushSink<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(10)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& pull = fg.emplaceBlock<gr::zeromq::ZmqPullSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(10)},
            {"bind", gr::pmt::Value(false)},
        }));
        auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(n_samples)},
        }));

        expect(fg.connect<"out">(source).to<"in">(push) == gr::ConnectionResult::SUCCESS);
        expect(fg.connect<"out">(pull).to<"in">(sink) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_with_timeout(sched, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, n_samples));
    };

    "Loopback vector<complex<float>>"_test = [] {
        gr::Graph fg;
        using T = std::complex<float>;
        constexpr gr::Size_t n_samples = 128;
        const auto endpoint = endpoint_for(1);

        auto& source = fg.emplaceBlock<VectorSource<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(n_samples)},
            {"payload_len", gr::pmt::Value(static_cast<gr::Size_t>(16))},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(500))},
        }));
        auto& push = fg.emplaceBlock<gr::zeromq::ZmqPushSink<std::vector<T>>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(10)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& pull = fg.emplaceBlock<gr::zeromq::ZmqPullSource<std::vector<T>>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(10)},
            {"bind", gr::pmt::Value(false)},
        }));
        auto& sink = fg.emplaceBlock<VectorSink<T>>(make_props({
            {"n_samples_max", gr::pmt::Value(n_samples)},
        }));

        expect(fg.connect<"out">(source).to<"in">(push) == gr::ConnectionResult::SUCCESS);
        expect(fg.connect<"out">(pull).to<"in">(sink) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_with_timeout(sched, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, n_samples));
        expect(eq(sink.last_size, static_cast<gr::Size_t>(16)));
    };

    "Loopback pmt::Value"_test = [] {
        gr::Graph fg;
        constexpr gr::Size_t n_samples = 64;
        const auto endpoint = endpoint_for(2);

        auto& source = fg.emplaceBlock<PmtSource>(make_props({
            {"n_samples_max", gr::pmt::Value(n_samples)},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(500))},
        }));
        auto& push = fg.emplaceBlock<gr::zeromq::ZmqPushSink<gr::pmt::Value>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(10)},
            {"bind", gr::pmt::Value(true)},
        }));
        auto& pull = fg.emplaceBlock<gr::zeromq::ZmqPullSource<gr::pmt::Value>>(make_props({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(10)},
            {"bind", gr::pmt::Value(false)},
        }));
        auto& sink = fg.emplaceBlock<PmtSink>(make_props({
            {"n_samples_max", gr::pmt::Value(n_samples)},
        }));

        expect(fg.connect<"out">(source).to<"in">(push) == gr::ConnectionResult::SUCCESS);
        expect(fg.connect<"out">(pull).to<"in">(sink) == gr::ConnectionResult::SUCCESS);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_with_timeout(sched, std::chrono::milliseconds(2000)));
        expect(eq(sink.count, n_samples));
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
