#include "ExampleHelpers.hpp"

#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>

int main(int argc, char** argv) {
    using namespace gr::incubator::zeromq::examples;

    require_role(argc, argv, "usage: zmq_push_pull push|pull [endpoint]");
    const std::string role = argv[1];
    const std::string endpoint = argc > 2 ? argv[2] : "tcp://127.0.0.1:5555";

    gr::Graph graph;
    if (role == "push") {
        auto& source = graph.emplaceBlock<SequenceSource<float>>();
        source.values = {1.f, 2.f, 3.f, 4.f};
        source.repeat = true;
        auto& push = graph.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<float>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", true},
        });
        if (auto conn = graph.connect<"out", "in">(source, push); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else if (role == "pull") {
        auto& pull = graph.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<float>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", false},
        });
        auto& sink = graph.emplaceBlock<PrintingSink<float>>();
        sink.n_items_max = 16;
        sink.label = "pull";
        if (auto conn = graph.connect<"out", "in">(pull, sink); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else {
        throw std::runtime_error("role must be push or pull");
    }

    run_graph<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>(std::move(graph));
}
