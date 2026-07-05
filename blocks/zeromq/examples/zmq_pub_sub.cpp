#include "ExampleHelpers.hpp"

#include <gnuradio-4.0/zeromq/ZmqPubSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqSubSource.hpp>

int main(int argc, char** argv) {
    using namespace gr::incubator::zeromq::examples;

    require_role(argc, argv, "usage: zmq_pub_sub pub|sub [endpoint] [topic]");
    const std::string role = argv[1];
    const std::string endpoint = argc > 2 ? argv[2] : "tcp://127.0.0.1:5556";
    const std::string topic = argc > 3 ? argv[3] : "demo";

    gr::Graph graph;
    if (role == "pub") {
        auto& source = graph.emplaceBlock<SequenceSource<float>>();
        source.values = {10.f, 20.f, 30.f, 40.f};
        source.repeat = true;
        auto& pub = graph.emplaceBlock<gr::incubator::zeromq::ZmqPubSink<float>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", true},
            {"key", topic},
        });
        if (auto conn = graph.connect<"out", "in">(source, pub); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else if (role == "sub") {
        auto& sub = graph.emplaceBlock<gr::incubator::zeromq::ZmqSubSource<float>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", false},
            {"key", topic},
        });
        auto& sink = graph.emplaceBlock<PrintingSink<float>>();
        sink.n_items_max = 16;
        sink.label = "sub";
        if (auto conn = graph.connect<"out", "in">(sub, sink); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else {
        throw std::runtime_error("role must be pub or sub");
    }

    run_graph<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>(std::move(graph));
}
