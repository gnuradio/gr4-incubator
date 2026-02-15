#include <complex>
#include <cstdint>
#include <cstdlib>
#include <memory_resource>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/basic/Converters.hpp>
#include <gnuradio-4.0/zeromq/ZmqPullSource.hpp>
#include <gnuradio-4.0/soapysdr/SoapyRx.hpp>
#include <gnuradio-4.0/fileio/BasicFileIo.hpp>
#include <gnuradio-4.0/analog/QuadratureDemod.hpp>
#include <gnuradio-4.0/analog/FmDeemphasisFilter.hpp>
#include <gnuradio-4.0/audio/RtAudioSink.hpp>
#include <gnuradio-4.0/pfb/PfbArbResampler.hpp>
#include <gnuradio-4.0/pfb/PfbArbResamplerTaps.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include <CLI/CLI.hpp> 
#include <cmath>
#include <print>

using namespace gr;
using namespace gr::basic;

namespace {
gr::property_map make_props(std::initializer_list<std::pair<std::string_view, gr::pmt::Value>> init) {
    gr::property_map out;
    auto*            mr = out.get_allocator().resource();
    for (const auto& [key, value] : init) {
        out.emplace(gr::pmt::Value::Map::value_type{std::pmr::string(key.data(), key.size(), mr), value});
    }
    return out;
}

void set_prop(gr::property_map& map, std::string_view key, gr::pmt::Value value) {
    auto* mr = map.get_allocator().resource();
    map[std::pmr::string(key.data(), key.size(), mr)] = std::move(value);
}
} // namespace

int main(int argc, char** argv) {
    CLI::App app{"Audio File Source example through ZMQ "};

    std::string filename;
    double      quad_rate = 400e3;
    std::string source_type = "file";
    bool        repeat_file = false;
    std::string zmq_endpoint = "tcp://localhost:5557";
    int         zmq_timeout = 10;
    bool        zmq_bind = true;
    std::string soapy_driver;
    std::string soapy_args;
    double      soapy_freq = 96e6;
    double      soapy_bw = 200e3;
    double      soapy_gain = 10.0;
    std::string soapy_antenna;
    std::size_t soapy_channel = 0;
    std::size_t audio_frames_per_buf = 0;
    double      audio_latency_s = 0.0;
    bool        audio_ignore_tag_sample_rate = false;

    app.add_option("--source", source_type, "Input source: file|zmq|soapy")
        ->check(CLI::IsMember({"file", "zmq", "soapy"}));
    app.add_option("-f,--file", filename, "Input file (fc32)")
        ->check(CLI::ExistingFile);
    app.add_option("--repeat", repeat_file, "Repeat file source");
    app.add_option("-r,--rate", quad_rate, "Sample rate (input sample rate in Hz)");
    app.add_option("--zmq-endpoint", zmq_endpoint, "ZMQ PULL endpoint");
    app.add_option("--zmq-timeout", zmq_timeout, "ZMQ poll timeout (ms)");
    app.add_option("--zmq-bind", zmq_bind, "ZMQ bind (true) or connect (false)");
    app.add_option("--soapy-driver", soapy_driver, "SoapySDR driver (e.g., rtlsdr)");
    app.add_option("--soapy-args", soapy_args, "SoapySDR device args");
    app.add_option("--soapy-freq", soapy_freq, "SoapySDR center frequency (Hz)");
    app.add_option("--soapy-bw", soapy_bw, "SoapySDR bandwidth (Hz)");
    app.add_option("--soapy-gain", soapy_gain, "SoapySDR gain (dB)");
    app.add_option("--soapy-antenna", soapy_antenna, "SoapySDR antenna name");
    app.add_option("--soapy-channel", soapy_channel, "SoapySDR RX channel index");
    app.add_option("--audio-frames-per-buf", audio_frames_per_buf, "RtAudio frames per buffer (0 = default)");
    app.add_option("--audio-latency", audio_latency_s, "RtAudio target latency seconds (0 = default)");
    app.add_flag("--audio-ignore-tag-sample-rate", audio_ignore_tag_sample_rate, "Ignore sample_rate tags in RtAudioSink");

    CLI11_PARSE(app, argc, argv);


    // using T = std::vector<uint8_t>;
    using TR = float;
    using T = std::complex<TR>;
    

    gr::Graph fg;


    double max_dev = 75e3;
    double fm_demod_gain = quad_rate / (2 * M_PI * max_dev);


    auto& quad_demod = fg.emplaceBlock<gr::analog::QuadratureDemod<TR>>(
        make_props({{"gain", gr::pmt::Value(fm_demod_gain)}}));


    auto& deemph_filter = fg.emplaceBlock<gr::analog::FmDeemphasisFilter<TR>>(
        make_props({{"sample_rate", gr::pmt::Value(static_cast<float>(quad_rate))}, {"tau", gr::pmt::Value(75e-6f)}}));


    double stop_band_attenuation = 80.0;
    double rate = 32e3 / quad_rate;
    size_t num_filters = 32;

    auto taps_vec = gr::pfb::create_taps<TR>(rate, num_filters, stop_band_attenuation);
    auto taps_val = gr::pmt::Value(gr::Tensor<TR>(gr::data_from, taps_vec));
    gr::property_map resamp_props = make_props({
        {"rate", gr::pmt::Value(rate)},
        {"taps", std::move(taps_val)},
        {"num_filters", gr::pmt::Value(num_filters)},
        {"stop_band_attenuation", gr::pmt::Value(stop_band_attenuation)},
    });

    auto& resampler = fg.emplaceBlock<gr::pfb::PfbArbResampler<TR>>(resamp_props);


    gr::property_map audio_props = make_props({
        {"sample_rate", gr::pmt::Value(32000)},
        {"channels_fallback", gr::pmt::Value(1)},
        {"device_index", gr::pmt::Value(-1)},
    });
    if (audio_frames_per_buf > 0) {
        set_prop(audio_props, "frames_per_buf", gr::pmt::Value(static_cast<uint32_t>(audio_frames_per_buf)));
    }
    if (audio_latency_s > 0.0) {
        set_prop(audio_props, "target_latency_s", gr::pmt::Value(audio_latency_s));
    }
    if (audio_ignore_tag_sample_rate) {
        set_prop(audio_props, "ignore_tag_sample_rate", gr::pmt::Value(true));
    }
    auto& audio_sink = fg.emplaceBlock<gr::audio::RtAudioSink<TR>>(audio_props);

    const char* connection_error = "connection_error";

    if (source_type == "file") {
        if (filename.empty()) {
            throw std::runtime_error("source=file requires --file");
        }
        auto& source = fg.emplaceBlock<gr::blocks::fileio::BasicFileSource<T>>(make_props({
            {"file_name", gr::pmt::Value(filename)},
            {"repeat", gr::pmt::Value(repeat_file)},
            {"disconnect_on_done", gr::pmt::Value(true)},
        }));
        if (fg.connect<"out">(source).to<"in">(quad_demod) != gr::ConnectionResult::SUCCESS) {
            throw gr::exception(connection_error);
        }
    } else if (source_type == "zmq") {
        auto& source = fg.emplaceBlock<gr::zeromq::ZmqPullSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(zmq_endpoint)},
            {"timeout", gr::pmt::Value(zmq_timeout)},
            {"bind", gr::pmt::Value(zmq_bind)},
        }));
        if (fg.connect<"out">(source).to<"in">(quad_demod) != gr::ConnectionResult::SUCCESS) {
            throw gr::exception(connection_error);
        }
    } else if (source_type == "soapy") {
        auto& source = fg.emplaceBlock<gr::soapysdr::SoapyRx<T>>(make_props({
            {"device", gr::pmt::Value(soapy_driver)},
            {"device_args", gr::pmt::Value(soapy_args)},
            {"sample_rate", gr::pmt::Value(static_cast<float>(quad_rate))},
            {"channel", gr::pmt::Value(static_cast<gr::Size_t>(soapy_channel))},
            {"center_frequency", gr::pmt::Value(soapy_freq)},
            {"bandwidth", gr::pmt::Value(soapy_bw)},
            {"gain", gr::pmt::Value(soapy_gain)},
            {"antenna", gr::pmt::Value(soapy_antenna)},
        }));
        if (fg.connect<"out">(source).to<"in">(quad_demod) != gr::ConnectionResult::SUCCESS) {
            throw gr::exception(connection_error);
        }
    } else {
        throw std::runtime_error("unknown source type");
    }


    if (fg.connect<"out">(quad_demod).to<"in">(deemph_filter) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(deemph_filter).to<"in">(resampler) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }

    // if (fg.connect<"out">(quad_demod).to<"in">(resampler) != gr::ConnectionResult::SUCCESS) {
    //     throw gr::exception(connection_error);
    // }


    if (fg.connect<"out">(resampler).to<"in">(audio_sink) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }


    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
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
