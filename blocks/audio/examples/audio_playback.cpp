#include <complex>
#include <cstdint>
#include <cstdlib>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/audio/Copy.hpp>
#include <gnuradio-4.0/audio/AudioFileSource.hpp>
#include <gnuradio-4.0/audio/RtAudioSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqPushSink.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include <print>
#include <CLI/CLI.hpp> 

using namespace gr;
using namespace gr::audio;

int main(int argc, char** argv) {
    CLI::App app{"Audio File Source example through ZMQ "};

    std::string filename;
    int zmq_port = 5556;

    app.add_option("-f,--file", filename, "Input file (wav, mp3, flac)")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("-z,--zmq_port", zmq_port, "ZMQ push zmq_port (default: 5556)");


    CLI11_PARSE(app, argc, argv);

    using T = float;

    gr::Graph fg;
    auto&     source = fg.emplaceBlock<AudioFileSource<T>>({
        {"file_name", filename},
        {"repeat", true}
    });

    // auto& sink = fg.emplaceBlock<gr::zeromq::ZmqPushSink<T>>({
    //     {"endpoint", "tcp://localhost:" + std::to_string(zmq_port)},
    //     {"timeout", 100},
    //     {"bind", true},
    // });

    auto& sink = fg.emplaceBlock<gr::audio::RtAudioSink<T>>({
        // {"channels_fallback", 2},
        // {"sample_rate", 48000}
    });



    const char* connection_error = "connection_error";


    if (fg.connect<"out">(source).to<"in">(sink) != gr::ConnectionResult::SUCCESS) {
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
