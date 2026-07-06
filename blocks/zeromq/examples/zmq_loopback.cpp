#include <complex>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>

namespace {

struct Options {
    std::string type = "float";
    std::string in_endpoint = "tcp://127.0.0.1:5555";
    std::string out_endpoint = "tcp://127.0.0.1:5556";
    bool        in_bind = false;
    bool        out_bind = true;
    bool        pass_tags = true;
};

bool parse_bool(std::string_view value) {
    if (value == "1" || value == "true" || value == "True" || value == "bind") {
        return true;
    }
    if (value == "0" || value == "false" || value == "False" || value == "connect") {
        return false;
    }
    throw std::runtime_error(std::format("invalid boolean value: {}", value));
}

void usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0 << " [type] [in_endpoint] [out_endpoint] [in_bind] [out_bind] [pass_tags]\n"
        << "\n"
        << "types: byte, short, int, float, complex\n"
        << "\n"
        << "Defaults are arranged for a GNU Radio 3 tagged stream round trip:\n"
        << "  GR3 zeromq.push_sink binds tcp://127.0.0.1:5555\n"
        << "  GR4 zmq_loopback connects input, preserves tags, and binds output\n"
        << "  GR3 zeromq.pull_source connects tcp://127.0.0.1:5556\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    if (argc > 1 && (std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help")) {
        usage(argv[0]);
        std::exit(0);
    }
    if (argc > 1) {
        options.type = argv[1];
    }
    if (argc > 2) {
        options.in_endpoint = argv[2];
    }
    if (argc > 3) {
        options.out_endpoint = argv[3];
    }
    if (argc > 4) {
        options.in_bind = parse_bool(argv[4]);
    }
    if (argc > 5) {
        options.out_bind = parse_bool(argv[5]);
    }
    if (argc > 6) {
        options.pass_tags = parse_bool(argv[6]);
    }
    return options;
}

template<typename T>
void run_loopback(const Options& options) {
    gr::Graph graph;
    auto& pull = graph.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<T>>({
        {"endpoint", options.in_endpoint},
        {"timeout", 100},
        {"bind", options.in_bind},
        {"pass_tags", options.pass_tags},
    });
    auto& push = graph.emplaceBlock<gr::incubator::zeromq::ZmqPushSink<T>>({
        {"endpoint", options.out_endpoint},
        {"timeout", 100},
        {"bind", options.out_bind},
        {"pass_tags", options.pass_tags},
    });

    if (auto conn = graph.connect<"out", "in">(pull, push); !conn) {
        throw std::runtime_error(conn.error().message);
    }

    std::cout << "zmq_loopback type=" << options.type
              << " in=" << options.in_endpoint << (options.in_bind ? " bind" : " connect")
              << " out=" << options.out_endpoint << (options.out_bind ? " bind" : " connect")
              << " pass_tags=" << (options.pass_tags ? "true" : "false") << '\n';

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> scheduler;
    if (auto ret = scheduler.exchange(std::move(graph)); !ret) {
        throw std::runtime_error(std::format("failed to initialize scheduler: {}", ret.error()));
    }
    if (auto ret = scheduler.runAndWait(); !ret) {
        throw std::runtime_error(std::format("scheduler error: {}", ret.error()));
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);

    if (options.type == "byte") {
        run_loopback<std::uint8_t>(options);
    } else if (options.type == "short") {
        run_loopback<std::int16_t>(options);
    } else if (options.type == "int") {
        run_loopback<std::int32_t>(options);
    } else if (options.type == "float") {
        run_loopback<float>(options);
    } else if (options.type == "complex") {
        run_loopback<std::complex<float>>(options);
    } else {
        usage(argv[0]);
        throw std::runtime_error(std::format("unsupported type: {}", options.type));
    }

    return 0;
}
