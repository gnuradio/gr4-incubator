#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <format>
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
#include <gnuradio-4.0/filter/FirDecimator.hpp>
#include <gnuradio-4.0/math/Rotator.hpp>
#include <gnuradio-4.0/pfb/PfbArbResampler.hpp>
#include <gnuradio-4.0/pfb/PfbArbResamplerTaps.hpp>
#include <gnuradio-4.0/scheduler/BlockingBackoff.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include <CLI/CLI.hpp> 
#include <cmath>
#include <optional>
#include <print>

using namespace gr;
using namespace gr::incubator::basic;

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
    double      rf_sample_rate = 2e6;
    double      quad_rate = 400e3;
    double      audio_rate = 32e3;
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
    std::optional<double> station_freq;
    double      frequency_shift = 0.0;
    uint32_t    channel_decim = 0;
    double      channel_cutoff = 120e3;
    double      channel_transition = 60e3;
    double      channel_attenuation = 30.0;
    std::size_t audio_frames_per_buf = 0;
    double      audio_latency_s = 0.0;
    std::string audio_api = "default";
    bool        audio_ignore_tag_sample_rate = false;

    app.add_option("--source", source_type, "Input source: file|zmq|soapy")
        ->check(CLI::IsMember({"file", "zmq", "soapy"}));
    app.add_option("-f,--file", filename, "Input file (fc32)")
        ->check(CLI::ExistingFile);
    app.add_option("--repeat", repeat_file, "Repeat file source");
    app.add_option("-r,--rate", rf_sample_rate, "RF/input sample rate in Hz");
    app.add_option("--quad-rate", quad_rate, "Quadrature demod input rate after channel decimation (Hz)");
    app.add_option("--audio-rate", audio_rate, "Audio sample rate (Hz)");
    app.add_option("--zmq-endpoint", zmq_endpoint, "ZMQ PULL endpoint");
    app.add_option("--zmq-timeout", zmq_timeout, "ZMQ poll timeout (ms)");
    app.add_option("--zmq-bind", zmq_bind, "ZMQ bind (true) or connect (false)");
    app.add_option("--soapy-driver", soapy_driver, "SoapySDR driver (e.g., rtlsdr)");
    app.add_option("--soapy-args", soapy_args, "SoapySDR device args");
    app.add_option("--soapy-freq", soapy_freq, "SoapySDR center frequency (Hz)");
    app.add_option("--station-freq", station_freq, "FM station frequency (Hz); sets rotator shift from Soapy center");
    app.add_option("--frequency-shift", frequency_shift, "Explicit frequency shift for file/ZMQ or when --station-freq is omitted (Hz)");
    app.add_option("--soapy-bw", soapy_bw, "SoapySDR bandwidth (Hz)");
    app.add_option("--soapy-gain", soapy_gain, "SoapySDR gain (dB)");
    app.add_option("--soapy-antenna", soapy_antenna, "SoapySDR antenna name");
    app.add_option("--soapy-channel", soapy_channel, "SoapySDR RX channel index");
    app.add_option("--channel-decim", channel_decim, "RF channel decimation factor (0 = round RF rate / quad rate)");
    app.add_option("--channel-cutoff", channel_cutoff, "FirDecimator low-pass cutoff frequency (Hz)");
    app.add_option("--channel-transition", channel_transition, "FirDecimator transition width (Hz)");
    app.add_option("--channel-attenuation", channel_attenuation, "FirDecimator stop-band attenuation (dB)");
    app.add_option("--audio-frames-per-buf", audio_frames_per_buf, "RtAudio frames per buffer (0 = default)");
    app.add_option("--audio-latency", audio_latency_s, "RtAudio target latency seconds (0 = default)");
    app.add_option("--audio-api", audio_api, "RtAudio API: default|alsa|pulse|jack|oss|dummy");
    app.add_flag("--audio-ignore-tag-sample-rate", audio_ignore_tag_sample_rate, "Ignore sample_rate tags in RtAudioSink");

    CLI11_PARSE(app, argc, argv);


    // using T = std::vector<uint8_t>;
    using TR = float;
    using T = std::complex<TR>;
    

    gr::Graph fg;


    const auto effective_channel_decim = channel_decim == 0U
        ? static_cast<uint32_t>(std::max(1.0, std::round(rf_sample_rate / quad_rate)))
        : channel_decim;
    quad_rate = rf_sample_rate / static_cast<double>(effective_channel_decim);
    const auto effective_frequency_shift = station_freq ? soapy_freq - *station_freq : frequency_shift;

    double max_dev = 75e3;
    double fm_demod_gain = quad_rate / (2 * M_PI * max_dev);

    auto& rotator = fg.emplaceBlock<gr::blocks::math::Rotator<T>>(make_props({
        {"sample_rate", gr::pmt::Value(static_cast<float>(rf_sample_rate))},
        {"frequency_shift", gr::pmt::Value(static_cast<float>(effective_frequency_shift))},
    }));

    auto& channel_decimator = fg.emplaceBlock<gr::incubator::filter::FirDecimator<T>>(make_props({
        {"decim", gr::pmt::Value(effective_channel_decim)},
        {"f_low", gr::pmt::Value(static_cast<float>(channel_cutoff))},
        {"sample_rate", gr::pmt::Value(static_cast<float>(rf_sample_rate))},
        {"transition_width", gr::pmt::Value(static_cast<float>(channel_transition))},
        {"num_taps", gr::pmt::Value(uint32_t{0})},
        {"attenuation_db", gr::pmt::Value(static_cast<float>(channel_attenuation))},
    }));

    auto& quad_demod = fg.emplaceBlock<gr::incubator::analog::QuadratureDemod<TR>>(
        make_props({{"gain", gr::pmt::Value(fm_demod_gain)}}));


    auto& deemph_filter = fg.emplaceBlock<gr::incubator::analog::FmDeemphasisFilter<TR>>(
        make_props({{"sample_rate", gr::pmt::Value(static_cast<float>(quad_rate))}, {"tau", gr::pmt::Value(75e-6f)}}));


    double stop_band_attenuation = 80.0;
    double rate = audio_rate / quad_rate;
    size_t num_filters = 32;

    auto taps_vec = gr::incubator::pfb::create_taps<TR>(rate, num_filters, stop_band_attenuation);
    auto taps_val = gr::pmt::Value(gr::Tensor<TR>(gr::data_from, taps_vec));
    gr::property_map resamp_props = make_props({
        {"rate", gr::pmt::Value(rate)},
        {"taps", std::move(taps_val)},
        {"num_filters", gr::pmt::Value(num_filters)},
        {"stop_band_attenuation", gr::pmt::Value(stop_band_attenuation)},
    });

    auto& resampler = fg.emplaceBlock<gr::incubator::pfb::PfbArbResampler<TR>>(resamp_props);


    gr::property_map audio_props = make_props({
        {"sample_rate", gr::pmt::Value(static_cast<float>(audio_rate))},
        {"channels_fallback", gr::pmt::Value(1)},
        {"device_index", gr::pmt::Value(-1)},
        {"audio_api", gr::pmt::Value(audio_api)},
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
    auto& audio_sink = fg.emplaceBlock<gr::incubator::audio::RtAudioSink<TR>>(audio_props);
    if (source_type == "file") {
        if (filename.empty()) {
            throw std::runtime_error("source=file requires --file");
        }
        auto& source = fg.emplaceBlock<gr::blocks::fileio::BasicFileSource<T>>(make_props({
            {"file_name", gr::pmt::Value(filename)},
            {"repeat", gr::pmt::Value(repeat_file)},
            {"disconnect_on_done", gr::pmt::Value(true)},
        }));
        if (auto conn = fg.connect<"out", "in">(source, rotator); !conn) {
            throw gr::exception(std::format("connect failed: {}", conn.error().message));
        }
    } else if (source_type == "zmq") {
        auto& source = fg.emplaceBlock<gr::incubator::zeromq::ZmqPullSource<T>>(make_props({
            {"endpoint", gr::pmt::Value(zmq_endpoint)},
            {"timeout", gr::pmt::Value(zmq_timeout)},
            {"bind", gr::pmt::Value(zmq_bind)},
        }));
        if (auto conn = fg.connect<"out", "in">(source, rotator); !conn) {
            throw gr::exception(std::format("connect failed: {}", conn.error().message));
        }
    } else if (source_type == "soapy") {
        auto& source = fg.emplaceBlock<gr::incubator::soapysdr::SoapyRx<T>>(make_props({
            {"device", gr::pmt::Value(soapy_driver)},
            {"device_args", gr::pmt::Value(soapy_args)},
            {"sample_rate", gr::pmt::Value(static_cast<float>(rf_sample_rate))},
            {"channel", gr::pmt::Value(static_cast<gr::Size_t>(soapy_channel))},
            {"center_frequency", gr::pmt::Value(soapy_freq)},
            {"bandwidth", gr::pmt::Value(soapy_bw)},
            {"gain", gr::pmt::Value(soapy_gain)},
            {"antenna", gr::pmt::Value(soapy_antenna)},
        }));
        if (auto conn = fg.connect<"out", "in">(source, rotator); !conn) {
            throw gr::exception(std::format("connect failed: {}", conn.error().message));
        }
    } else {
        throw std::runtime_error("unknown source type");
    }

    if (auto conn = fg.connect<"out", "in">(rotator, channel_decimator); !conn) {
        throw gr::exception(std::format("connect failed: {}", conn.error().message));
    }
    if (auto conn = fg.connect<"out", "in">(channel_decimator, quad_demod); !conn) {
        throw gr::exception(std::format("connect failed: {}", conn.error().message));
    }
    if (auto conn = fg.connect<"out", "in">(quad_demod, deemph_filter); !conn) {
        throw gr::exception(std::format("connect failed: {}", conn.error().message));
    }
    if (auto conn = fg.connect<"out", "in">(deemph_filter, resampler); !conn) {
        throw gr::exception(std::format("connect failed: {}", conn.error().message));
    }

    if (auto conn = fg.connect<"out", "in">(resampler, audio_sink); !conn) {
        throw gr::exception(std::format("connect failed: {}", conn.error().message));
    }


    gr::incubator::scheduler::BlockingBackoff<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
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
