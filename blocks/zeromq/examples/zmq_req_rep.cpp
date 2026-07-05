#include "ExampleHelpers.hpp"

#include <gnuradio-4.0/zeromq/ZmqRepSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqReqSource.hpp>

int main(int argc, char** argv) {
    using namespace gr::incubator::zeromq::examples;

    require_role(argc, argv, "usage: zmq_req_rep req|rep [endpoint]");
    const std::string role = argv[1];
    const std::string endpoint = argc > 2 ? argv[2] : "tcp://127.0.0.1:5557";

    gr::Graph graph;
    if (role == "rep") {
        auto& source = graph.emplaceBlock<SequenceSource<float>>();
        source.values = {100.f, 200.f, 300.f, 400.f};
        source.repeat = true;
        auto& rep = graph.emplaceBlock<gr::incubator::zeromq::ZmqRepSink<float>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", true},
        });
        if (auto conn = graph.connect<"out", "in">(source, rep); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else if (role == "req") {
        auto& req = graph.emplaceBlock<gr::incubator::zeromq::ZmqReqSource<float>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", false},
        });
        auto& sink = graph.emplaceBlock<PrintingSink<float>>();
        sink.n_items_max = 16;
        sink.label = "req";
        if (auto conn = graph.connect<"out", "in">(req, sink); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else {
        throw std::runtime_error("role must be req or rep");
    }

    run_graph<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>(std::move(graph));
}
