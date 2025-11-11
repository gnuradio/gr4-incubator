#include <complex>
#include <cstdint>
#include <cstdlib>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/basic/Converters.hpp>
#include <gnuradio-4.0/fileio/BasicFileIo.hpp>
#include <gnuradio-4.0/analog/QuadratureDemod.hpp>
#include <gnuradio-4.0/filter/time_domain_filter.hpp>
#include <gnuradio-4.0/audio/RtAudioSink.hpp>
#include <gnuradio-4.0/pfb/PfbArbResampler.hpp>
#include <gnuradio-4.0/pfb/PfbArbResamplerTaps.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include <CLI/CLI.hpp> 
#include <cmath>
#include <print>

using namespace gr;
using namespace gr::basic;

int main(int argc, char** argv) {
    CLI::App app{"Audio File Source example through ZMQ "};

    std::string filename;
    double quad_rate = 400e3;

    app.add_option("-f,--file", filename, "Input file (fc32)")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("-r,--rate", quad_rate, "Sample Rate of file");

    CLI11_PARSE(app, argc, argv);


    // using T = std::vector<uint8_t>;
    using TR = float;
    using T = std::complex<TR>;
    

    gr::Graph fg;


    double max_dev = 75e3;
    double fm_demod_gain = quad_rate / (2 * M_PI * max_dev);


    auto&   source = fg.emplaceBlock<gr::blocks::fileio::BasicFileSource<T>>({
        {"file_name", filename},
        {"repeat", false},
        {"disconnect_on_done", true},
    });

    auto& quad_demod = fg.emplaceBlock<gr::analog::QuadratureDemod<TR>>({
        {"gain", fm_demod_gain},
    });

    // local audio playback; no ZMQ blocks in this flowgraph


    // Deemphasis filter is just an iir filter with the following ataps and btaps
    double w_c = 1.0 / max_dev;
    double w_ca = 2.0 * quad_rate * std::tan(w_c / (2.0 * quad_rate));
    double k = -w_ca / (2.0 * quad_rate);
    double z1 = -1.0;
    double p1 = (1.0 + k) / (1.0 - k);
    double b0 = -k / (1.0 - k);

    std::vector<float> btaps{b0 * 1.0, b0 * -z1};
    std::vector<float> ataps{1.0, -p1};

    auto& iir_filter = fg.emplaceBlock<gr::filter::iir_filter<TR>>({
        {"b", btaps},
        {"a", ataps},
    });

    // def create_taps(numchans, atten=100):
    //     # Create a filter that covers the full bandwidth of the input signal
    //     bw = 0.4
    //     tb = 0.2
    //     ripple = 0.1
    //     while True:
    //         try:
    //             taps = optfir.low_pass(1, numchans, bw, bw + tb, ripple, atten)
    //             return taps
    //         except ValueError as e:
    //             # This shouldn't happen, unless numchans is strange
    //             raise RuntimeError("couldn't design filter; this probably constitutes a bug")
    //         except RuntimeError:
    //             ripple += 0.01
    //             print("Warning: set ripple to %.4f dB. If this is a problem, adjust the attenuation or create your own filter taps." % (ripple))

    //             # Build in an exit strategy; if we've come this far, it ain't working.
    //             if(ripple >= 1.0):
    //                 raise RuntimeError(
    //                     "optfir could not generate an appropriate filter.")


    double stop_band_attenuation = 80.0;
    double rate = 32e3 / quad_rate;
    size_t num_filters = 32;

    auto& resampler = fg.emplaceBlock<gr::pfb::PfbArbResampler<TR>>({
        {"rate", rate },
        {"taps", gr::pfb::create_taps<TR>(rate, num_filters, stop_band_attenuation)},
        {"num_filters", num_filters},
        {"stop_band_attenuation", stop_band_attenuation},
    });


    auto& audio_sink = fg.emplaceBlock<gr::audio::RtAudioSink<TR>>({
        {"sample_rate",32000},
        {"channels_fallback",1},
        {"device_index",-1}
    });

    const char* connection_error = "connection_error";

    // if (fg.connect<"out">(source).to<"in">(sink) != gr::ConnectionResult::SUCCESS) {
    //     throw gr::exception(connection_error);
    // }
    if (fg.connect<"out">(source).to<"in">(quad_demod) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(quad_demod).to<"in">(resampler) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }

    // if (fg.connect<"out">(zmq_source).to<"in">(audio_sink) != gr::ConnectionResult::SUCCESS) {
    //     throw gr::exception(connection_error);
    // }
    if (fg.connect<"out">(resampler).to<"in">(audio_sink) != gr::ConnectionResult::SUCCESS) {
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
