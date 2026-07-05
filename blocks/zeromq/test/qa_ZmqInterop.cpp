#include <boost/ut.hpp>

#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <future>
#include <memory_resource>
#include <optional>
#include <expected>
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
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqPubSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqReqSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqRepSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqSubSource.hpp>
#include <zmq.hpp>

using namespace boost::ut;

namespace {

std::string endpoint_for(int offset) {
    const int base = 52000 + (getpid() % 1000);
    return std::format("tcp://127.0.0.1:{}", base + offset);
}

gr::property_map make_props(std::initializer_list<std::pair<std::string_view, gr::pmt::Value>> init) {
    gr::property_map out;
    auto*            mr = out.get_allocator().resource();
    for (const auto& [key, value] : init) {
        out.emplace(gr::pmt::Value::Map::value_type{std::pmr::string(key.data(), key.size(), mr), value});
    }
    return out;
}

std::string shell_quote(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (const char c : value) {
        if (c == '\'') {
            out.append("'\"'\"'");
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

template <typename T>
struct SequenceSource : gr::Block<SequenceSource<T>> {
    gr::PortOut<T> out;
    std::vector<T> values;
    std::size_t    index = 0;
    gr::Size_t     startup_delay_ms = 50;
    bool           started = false;

    GR_MAKE_REFLECTABLE(SequenceSource, out, values, startup_delay_ms);

    [[nodiscard]] T processOne() {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        if (index >= values.size()) {
            this->requestStop();
            return T{};
        }

        auto value = values[index++];
        if (index >= values.size()) {
            this->requestStop();
        }
        return value;
    }
};

template <typename T>
struct LoopingSource : gr::Block<LoopingSource<T>> {
    gr::PortOut<T> out;
    std::vector<T> values;
    std::size_t    index = 0;
    gr::Size_t     startup_delay_ms = 50;
    bool           started = false;

    GR_MAKE_REFLECTABLE(LoopingSource, out, values, startup_delay_ms);

    [[nodiscard]] T processOne() {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        if (values.empty()) {
            this->requestStop();
            return T{};
        }
        const auto value = values[index % values.size()];
        ++index;
        return value;
    }
};

template <typename T>
struct RecordingSink : gr::Block<RecordingSink<T>> {
    gr::PortIn<T>   in;
    gr::Size_t      n_samples_max = 0;
    std::vector<T>  received;

    GR_MAKE_REFLECTABLE(RecordingSink, in, n_samples_max);

    void processOne(const T& value) {
        received.push_back(value);
        if (n_samples_max > 0 && received.size() >= n_samples_max) {
            this->requestStop();
        }
    }
};

struct RecordingTaggedSink : gr::Block<RecordingTaggedSink> {
    gr::PortIn<float> in;
    gr::Size_t        n_samples_max = 0;
    std::vector<float> received;
    std::vector<std::pair<std::size_t, gr::property_map>> tags;
    std::size_t consumed_total = 0;

    GR_MAKE_REFLECTABLE(RecordingTaggedSink, in, n_samples_max);

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& input) {
        const auto base_offset = consumed_total;
        for (const auto& value : input) {
            received.push_back(value);
        }
        for (const auto& [relIndex, tagMapRef] : input.tags()) {
            if (relIndex >= 0) {
                tags.emplace_back(base_offset + static_cast<std::size_t>(relIndex), tagMapRef.get());
            }
        }
        consumed_total += input.size();
        if (n_samples_max > 0 && received.size() >= n_samples_max) {
            this->requestStop();
        }
        return gr::work::Status::OK;
    }
};

template <typename T>
struct TaggedBulkSource : gr::Block<TaggedBulkSource<T>> {
    gr::PortOut<T> out;
    std::vector<T> values;
    std::vector<std::pair<std::size_t, gr::property_map>> tags;
    std::size_t emitted = 0;
    gr::Size_t startup_delay_ms = 50;
    bool started = false;

    GR_MAKE_REFLECTABLE(TaggedBulkSource, out, values, startup_delay_ms);

    [[nodiscard]] gr::work::Status processBulk(gr::OutputSpanLike auto& output) {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        const auto n = std::min(output.size(), values.size() - emitted);
        for (std::size_t i = 0; i < n; ++i) {
            output[i] = values[emitted + i];
        }
        for (const auto& [tag_index, tag_map] : tags) {
            if (tag_index >= emitted && tag_index < emitted + n) {
                output.publishTag(tag_map, tag_index - emitted);
            }
        }
        emitted += n;
        output.publish(n);
        if (emitted >= values.size()) {
            this->requestStop();
            return gr::work::Status::DONE;
        }
        return gr::work::Status::OK;
    }
};

template <typename T>
struct RepeatingTaggedBulkSource : gr::Block<RepeatingTaggedBulkSource<T>> {
    gr::PortOut<T> out;
    T value{};
    std::vector<std::pair<std::size_t, gr::property_map>> tags;
    gr::Size_t startup_delay_ms = 50;
    bool started = false;

    GR_MAKE_REFLECTABLE(RepeatingTaggedBulkSource, out, value, startup_delay_ms);

    [[nodiscard]] gr::work::Status processBulk(gr::OutputSpanLike auto& output) {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        for (auto& item : output) {
            item = value;
        }
        for (const auto& [tag_index, tag_map] : tags) {
            if (tag_index < output.size()) {
                output.publishTag(tag_map, tag_index);
            }
        }
        output.publish(output.size());
        return gr::work::Status::OK;
    }
};

template <typename SchedulerT>
bool run_until_then_stop(SchedulerT& sched, auto&& done, std::chrono::milliseconds timeout) {
    std::mutex                                mu;
    std::condition_variable                   cv;
    std::optional<std::expected<void, gr::Error>> result;
    bool                                      scheduler_done = false;

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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::ignore = sched.changeStateTo(gr::lifecycle::State::REQUESTED_STOP);

    if (runner.joinable()) {
        runner.join();
    }

    return predicate_done && result.has_value() && result->has_value();
}

gr::pmt::Value make_nested_pair() {
    gr::Tensor<gr::pmt::Value> tail{gr::pmt::Value(int32_t{3}), gr::pmt::Value{}};
    gr::Tensor<gr::pmt::Value> mid{gr::pmt::Value(int32_t{2}), gr::pmt::Value(tail)};
    gr::Tensor<gr::pmt::Value> head{gr::pmt::Value(int32_t{1}), gr::pmt::Value(mid)};
    return gr::pmt::Value(head);
}

gr::property_map wire_tag(std::string_view key, gr::pmt::Value value, gr::pmt::Value srcid = gr::pmt::Value{}) {
    return make_props({{"key", gr::pmt::Value(std::string(key))}, {"value", std::move(value)}, {"srcid", std::move(srcid)}});
}

std::string peer_script() {
    return (std::filesystem::path{__FILE__}.parent_path() / "qa_ZmqInterop_peer.py").string();
}

std::future<int> spawn_peer(std::string mode, std::string endpoint) {
    const auto cmd = std::format("python3 {} {} {}", shell_quote(peer_script()), shell_quote(mode), shell_quote(endpoint));
    return std::async(std::launch::async, [cmd = std::move(cmd)] { return std::system(cmd.c_str()); });
}

bool gr3_available() {
    const auto cmd = std::format("python3 {} {} {}", shell_quote(peer_script()), shell_quote("probe"), shell_quote("unused"));
    const auto rc = std::system(cmd.c_str());
    return rc == 0;
}

const bool has_gr3 = gr3_available();

} // namespace

const suite ZmqInteropTests = [] {
    if (!has_gr3) {
        return;
    }

    "GNU Radio 3 push_sink feeds GR4 pull_source"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(0);
        const auto expected = std::vector<float>{1.f, 2.f, 3.f, 4.f};

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        });
        auto& sink = fg.emplaceBlock<RecordingSink<float>>();
        sink.n_samples_max = expected.size();

        expect(fg.connect<"out", "in">(pull, sink).has_value());

        auto peer = spawn_peer("raw_push_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.received.size() >= expected.size(); }, std::chrono::milliseconds(5000)));
        expect(sink.received == expected);
        expect(eq(peer.get(), 0));
    };

    "GR4 push_sink feeds GNU Radio 3 pull_source"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(1);
        const auto expected = std::vector<float>{5.f, 6.f, 7.f, 8.f};

        auto& source = fg.emplaceBlock<LoopingSource<float>>({
            {"values", expected},
            {"startup_delay_ms", gr::pmt::Value(static_cast<gr::Size_t>(100))},
        });
        auto& push = fg.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        });

        expect(fg.connect<"out", "in">(source, push).has_value());

        auto peer = spawn_peer("raw_pull_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return peer.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(10000)));
        expect(eq(peer.get(), 0));
    };

    "GNU Radio 3 pub_sink feeds GR4 sub_source"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(4);
        const auto expected = std::vector<float>{1.f, 2.f, 3.f, 4.f};

        auto& sub = fg.emplaceBlock<gr::incubator::zeromq::ZmqSubSource<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
        });
        auto& sink = fg.emplaceBlock<RecordingSink<float>>();
        sink.n_samples_max = expected.size();

        expect(fg.connect<"out", "in">(sub, sink).has_value());

        auto peer = spawn_peer("raw_pub_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.received.size() >= expected.size(); }, std::chrono::milliseconds(5000)));
        expect(sink.received == expected);
        expect(eq(peer.get(), 0));
    };

    "GR4 pub_sink feeds GNU Radio 3 sub_source"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(5);
        const auto expected = std::vector<float>{5.f, 6.f, 7.f, 8.f};

        auto& source = fg.emplaceBlock<LoopingSource<float>>();
        source.values = expected;
        source.startup_delay_ms = 300;
        auto& pub = fg.emplaceBlock<gr::incubator::zeromq::ZmqPubSink<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        });

        expect(fg.connect<"out", "in">(source, pub).has_value());

        auto peer = spawn_peer("raw_sub_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return peer.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(10000)));
        expect(eq(peer.get(), 0));
    };

    "GNU Radio 3 req_source feeds GR4 rep_sink"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(6);
        const auto expected = std::vector<float>{5.f, 6.f, 7.f, 8.f};

        auto& source = fg.emplaceBlock<LoopingSource<float>>();
        source.values = expected;
        source.startup_delay_ms = 300;
        auto& rep = fg.emplaceBlock<gr::incubator::zeromq::ZmqRepSink<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        });

        expect(fg.connect<"out", "in">(source, rep).has_value());

        auto peer = spawn_peer("raw_req_source", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return peer.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(10000)));
        expect(eq(peer.get(), 0));
    };

    "GR4 req_source feeds GNU Radio 3 rep_sink"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(7);
        const auto expected = std::vector<float>{5.f, 6.f, 7.f, 8.f};

        auto& req = fg.emplaceBlock<gr::incubator::zeromq::ZmqReqSource<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        });
        auto& sink = fg.emplaceBlock<RecordingSink<float>>();
        sink.n_samples_max = expected.size();

        expect(fg.connect<"out", "in">(req, sink).has_value());

        auto peer = spawn_peer("raw_rep_sink", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.received.size() >= expected.size(); }, std::chrono::milliseconds(10000)));
        expect(sink.received == expected);
        expect(eq(peer.get(), 0));
    };

    "GNU Radio 3 push_msg_sink feeds GR4 pull_source<gr::pmt::Value>"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(2);
        const auto expected = make_nested_pair();

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<gr::pmt::Value>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        });
        auto& sink = fg.emplaceBlock<RecordingSink<gr::pmt::Value>>();
        sink.n_samples_max = 1;

        expect(fg.connect<"out", "in">(pull, sink).has_value());

        auto peer = spawn_peer("pmt_push_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.received.size() >= 1; }, std::chrono::milliseconds(5000)));
        expect(legacy_pmt::serialize_to_legacy(sink.received.front()) == legacy_pmt::serialize_to_legacy(expected));
        expect(eq(peer.get(), 0));
    };

    "GR4 push_sink<gr::pmt::Value> feeds GNU Radio 3 pull_msg_source"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(3);
        const auto expected = make_nested_pair();

        auto& source = fg.emplaceBlock<LoopingSource<gr::pmt::Value>>();
        source.values = std::vector<gr::pmt::Value>{expected};
        source.startup_delay_ms = 300;
        auto& push = fg.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<gr::pmt::Value>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
        });

        expect(fg.connect<"out", "in">(source, push).has_value());

        auto peer = spawn_peer("pmt_pull_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return peer.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(10000)));
        expect(eq(peer.get(), 0));
    };

    "GNU Radio 3 tags_strobe feeds GR4 pull_source with tag forwarding"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(8);

        auto& pull = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"pass_tags", gr::pmt::Value(true)},
        });
        auto& sink = fg.emplaceBlock<RecordingTaggedSink>();
        sink.n_samples_max = 4;

        expect(fg.connect<"out", "in">(pull, sink).has_value());

        auto peer = spawn_peer("tagged_push_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return sink.received.size() >= 4; }, std::chrono::milliseconds(5000)));
        expect(eq(peer.get(), 0));

        expect(!sink.tags.empty());
        if (!sink.tags.empty()) {
            expect(eq(sink.tags.front().first, static_cast<std::size_t>(0)));
            expect(eq(sink.tags.front().second.at("key").value_or(std::string{}), std::string{"packet_len"}));
            expect(eq(sink.tags.front().second.at("value").value_or<int32_t>(0), 4));
        }
    };

    "GR4 push_sink with same-offset tags feeds GNU Radio 3 pull_source"_test = [] {
        gr::Graph fg;
        const auto endpoint = endpoint_for(9);

        auto& source = fg.emplaceBlock<RepeatingTaggedBulkSource<float>>();
        source.value = 1.f;
        source.startup_delay_ms = 300;
        source.tags = {
            {0UZ, wire_tag("alpha", gr::pmt::Value(int32_t{11}), gr::pmt::Value("src-a"))},
            {0UZ, wire_tag("beta", gr::pmt::Value(int32_t{22}), gr::pmt::Value("src-b"))},
        };
        auto& push = fg.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<float>>({
            {"endpoint", gr::pmt::Value(endpoint)},
            {"timeout", gr::pmt::Value(25)},
            {"bind", gr::pmt::Value(true)},
            {"pass_tags", gr::pmt::Value(true)},
        });

        expect(fg.connect<"out", "in">(source, push).has_value());

        auto peer = spawn_peer("tagged_pull_multi_connect", endpoint);

        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(fg)).has_value());
        expect(run_until_then_stop(sched, [&] { return peer.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready; }, std::chrono::milliseconds(10000)));
        expect(eq(peer.get(), 0));
    };
};

int main() {}
