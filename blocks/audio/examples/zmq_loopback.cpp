#include <complex>
#include <cstdint>
#include <cstdlib>
#include <format>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include <print>

using namespace gr;
int main() {

 
    using T = float;

    gr::Graph fg;
    auto&     source = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<T>>({
        {"endpoint", "tcp://localhost:5555"},
        {"timeout", 10},
        {"bind", false},
    });

    auto& sink = fg.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<T>>({
        {"endpoint", "tcp://localhost:5556"},
        {"timeout", 100},
        {"bind", true},
    });
    if (auto conn = fg.connect<"out", "in">(source, sink); !conn) {
        throw gr::exception(std::format("connect failed: {}", conn.error().message));
    }


    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded> sched{std::move(fg)};
    const auto                                                            ret = sched.runAndWait();
    if (!ret.has_value()) {
        std::print("scheduler error: {}", ret.error());
        std::exit(1);
    }

    return 0;
}
