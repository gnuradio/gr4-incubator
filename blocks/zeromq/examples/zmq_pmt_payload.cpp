#include "ExampleHelpers.hpp"

#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>

struct PmtPrintingSink : gr::Block<PmtPrintingSink> {
    gr::PortIn<gr::pmt::Value> in;
    gr::Size_t                 n_items_max = 4;
    std::size_t                count = 0;

    GR_MAKE_REFLECTABLE(PmtPrintingSink, in, n_items_max);

    void processOne(const gr::pmt::Value& value) {
        const auto bytes = legacy_pmt::serialize_to_legacy(value);
        std::cout << "pmt[" << count << "] legacy_size=" << bytes.size() << '\n';
        ++count;
        if (n_items_max > 0 && count >= n_items_max) {
            this->requestStop();
        }
    }
};

int main(int argc, char** argv) {
    using namespace gr::incubator::zeromq::examples;

    require_role(argc, argv, "usage: zmq_pmt_payload push|pull [endpoint]");
    const std::string role = argv[1];
    const std::string endpoint = argc > 2 ? argv[2] : "tcp://127.0.0.1:5558";

    gr::Graph graph;
    if (role == "push") {
        auto& source = graph.emplaceBlock<SequenceSource<gr::pmt::Value>>();
        source.values = {
            gr::pmt::Value(std::string{"hello"}),
            gr::pmt::Value(int32_t{42}),
            gr::pmt::Value(std::string{"from gr4"}),
        };
        source.repeat = true;
        auto& push = graph.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<gr::pmt::Value>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", true},
        });
        if (auto conn = graph.connect<"out", "in">(source, push); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else if (role == "pull") {
        auto& pull = graph.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<gr::pmt::Value>>({
            {"endpoint", endpoint},
            {"timeout", 100},
            {"bind", false},
        });
        auto& sink = graph.emplaceBlock<PmtPrintingSink>();
        if (auto conn = graph.connect<"out", "in">(pull, sink); !conn) {
            throw std::runtime_error(conn.error().message);
        }
    } else {
        throw std::runtime_error("role must be push or pull");
    }

    run_graph<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>(std::move(graph));
}
