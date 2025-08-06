#include <boost/ut.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>

using namespace gr::zeromq;
using namespace boost::ut;
using namespace gr::testing;

const suite ZmqPushPullTests = [] {
    "Loopback Test"_test = [] {

        gr::Graph fg;
        using T = uint8_t;
        using TVec = std::vector<T>;
        size_t nsamples = 100;

        auto&     source    = fg.emplaceBlock<ZmqPullSource<TVec>>({{"n_samples_max", nsamples}});
        auto&     pull_source = fg.emplaceBlock<gr::zeromq::ZmqPullSource<TVec>>({
            {"endpoint", "tcp://localhost:5555"},
            {"timeout", 10},
            {"bind", false},
        });

        auto& push_sink = fg.emplaceBlock<gr::zeromq::ZmqPushSink<T>>({
            {"endpoint", "tcp://*:5555"},
            {"timeout", 100},
            {"bind", true},
        });

        auto&     sink      = fg.emplaceBlock<CountingSink<T>>();

        auto sched = gr::scheduler::Simple<>(std::move(fg));
        expect(sched.runAndWait().has_value());

        expect(eq(sink.count, static_cast<gr::Size_t>(nsamples)));

    };

};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }