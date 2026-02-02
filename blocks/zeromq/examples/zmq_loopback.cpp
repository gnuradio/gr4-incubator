#include <complex>
#include <cstdint>
#include <cstdlib>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/basic/Converters.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include <print>

using namespace gr;
using namespace gr::basic;

int main() {

    // using T = std::vector<uint8_t>;
    using T = std::complex<float>;

    gr::Graph fg;
    auto&     source = fg.emplaceBlock<gr::zeromq::ZmqPullSource<T>>({
        {"endpoint", "tcp://localhost:5555"},
        {"timeout", 10},
        {"bind", false},
    });

    // auto& sink = fg.emplaceBlock<gr::zeromq::ZmqPushSink<T>>({
    //     {"endpoint", "tcp://localhost:5556"},
    //     {"timeout", 100},
    //     {"bind", true},
    // });

    auto& s2pmt = fg.emplaceBlock<StreamToPmt<T>>({
        {"packet_size", 1024},
    });

    auto& sink = fg.emplaceBlock<gr::zeromq::ZmqPushSink<gr::pmt::Value>>({
        {"endpoint", "tcp://localhost:5556"},
        {"timeout", 100},
        {"bind", true},
    });


    const char* connection_error = "connection_error";

    // if (fg.connect<"out">(source).to<"in">(sink) != gr::ConnectionResult::SUCCESS) {
    //     throw gr::exception(connection_error);
    // }
    if (fg.connect<"out">(source).to<"in">(s2pmt) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(s2pmt).to<"in">(sink) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }


    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded> sched;
    if (auto ret = sched.exchange(std::move(fg)); !ret) {
                throw std::runtime_error(std::format("failed to initialize scheduler: {}", ret.error()));
    }

    const auto                                                            ret = sched.runAndWait();
    if (!ret.has_value()) {
        std::print("scheduler error: {}", ret.error());
        std::exit(1);
    }

    return 0;
}
