#include <complex>
#include <cstdint>
#include <cstdlib>
#include <charconv>
#include <cstdio>
#include <format>
#include <cmath>
#include <memory_resource>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <array>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/DataSink.hpp>
#include <gnuradio-4.0/soapysdr/SoapyRx.hpp>
#include <gnuradio-4.0/analog/QuadratureDemod.hpp>
#include <gnuradio-4.0/analog/FmDeemphasisFilter.hpp>
#include <gnuradio-4.0/audio/RtAudioSink.hpp>
#include <gnuradio-4.0/pfb/PfbArbResampler.hpp>
#include <gnuradio-4.0/pfb/PfbArbResamplerTaps.hpp>
#include <gnuradio-4.0/math/Math.hpp>

#include <CLI/CLI.hpp>

#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

using namespace gr;

namespace {

gr::property_map make_props(std::initializer_list<std::pair<std::string_view, gr::pmt::Value>> init) {
    gr::property_map out;
    auto*            mr = out.get_allocator().resource();
    for (const auto& [key, value] : init) {
        out.emplace(gr::pmt::Value::Map::value_type{std::pmr::string(key.data(), key.size(), mr), value});
    }
    return out;
}

std::optional<double> parse_with_suffix(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    double scale = 1.0;
    char   suffix = text.back();
    if (suffix == 'k' || suffix == 'K') {
        scale = 1e3;
        text.remove_suffix(1);
    } else if (suffix == 'm' || suffix == 'M') {
        scale = 1e6;
        text.remove_suffix(1);
    } else if (suffix == 'g' || suffix == 'G') {
        scale = 1e9;
        text.remove_suffix(1);
    }

    double value = 0.0;
    auto   result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc()) {
        return std::nullopt;
    }
    return value * scale;
}

std::string format_hz(double hz) {
    if (hz >= 1e9) {
        return std::format("{:.3f}G", hz / 1e9);
    }
    if (hz >= 1e6) {
        return std::format("{:.3f}M", hz / 1e6);
    }
    if (hz >= 1e3) {
        return std::format("{:.3f}k", hz / 1e3);
    }
    return std::format("{:.0f}", hz);
}

std::string format_variant(const gr::property_map& map, std::string_view key) {
    auto it = map.find(gr::pmt::Value::Map::key_type(std::string(key), map.get_allocator().resource()));
    if (it == map.end()) {
        return "<unset>";
    }
    const auto& v = it->second;
    if (auto d = v.get_if<double>()) {
        return std::format("{:.6f}", *d);
    }
    if (auto f = v.get_if<float>()) {
        return std::format("{:.6f}", static_cast<double>(*f));
    }
    if (auto u = v.get_if<uint64_t>()) {
        return std::format("{}", *u);
    }
    if (auto i = v.get_if<int64_t>()) {
        return std::format("{}", *i);
    }
    if (v.is_string()) {
        return std::string(v.value_or(std::string_view{}));
    }
    if (auto b = v.get_if<bool>()) {
        return *b ? "true" : "false";
    }
    return "<unsupported>";
}

BlockModel* find_block(gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>& sched, std::string_view name) {
    for (auto& block : sched.blocks()) {
        if (block && block->name() == name) {
            return block.get();
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"FM demodulator with ImGui controls"};

    std::string soapy_driver;
    std::string soapy_args;
    double      soapy_freq = 96e6;
    double      soapy_bw = 200e3;
    double      soapy_gain = 10.0;
    std::string soapy_antenna;
    std::size_t soapy_channel = 0;
    double      quad_rate = 400e3;
    double      audio_rate = 32e3;
    double      volume = 0.5;
    std::size_t audio_frames_per_buf = 0;
    double      audio_latency_s = 0.0;
    bool        audio_ignore_tag_sample_rate = false;

    app.add_option("--soapy-driver", soapy_driver, "SoapySDR driver (e.g., rtlsdr)");
    app.add_option("--soapy-args", soapy_args, "SoapySDR device args");
    app.add_option("--soapy-freq", soapy_freq, "SoapySDR center frequency (Hz)");
    app.add_option("--soapy-bw", soapy_bw, "SoapySDR bandwidth (Hz)");
    app.add_option("--soapy-gain", soapy_gain, "SoapySDR gain (dB)");
    app.add_option("--soapy-antenna", soapy_antenna, "SoapySDR antenna name");
    app.add_option("--soapy-channel", soapy_channel, "SoapySDR RX channel index");
    app.add_option("-r,--rate", quad_rate, "Input sample rate (Hz)");
    app.add_option("--audio-rate", audio_rate, "Audio sample rate (Hz)");
    app.add_option("--volume", volume, "Initial volume multiplier");
    app.add_option("--audio-frames-per-buf", audio_frames_per_buf, "RtAudio frames per buffer (0 = default)");
    app.add_option("--audio-latency", audio_latency_s, "RtAudio target latency seconds (0 = default)");
    app.add_flag("--audio-ignore-tag-sample-rate", audio_ignore_tag_sample_rate, "Ignore sample_rate tags in RtAudioSink");

    CLI11_PARSE(app, argc, argv);

    using TR = float;
    using T = std::complex<TR>;

    gr::Graph fg;

    double max_dev = 75e3;
    double fm_demod_gain = quad_rate / (2 * M_PI * max_dev);

    auto& soapy_rx = fg.emplaceBlock<gr::soapysdr::SoapyRx<T>>(make_props({
        {"name", gr::pmt::Value(std::string("soapy_rx"))},
        {"device", gr::pmt::Value(soapy_driver)},
        {"device_args", gr::pmt::Value(soapy_args)},
        {"sample_rate", gr::pmt::Value(static_cast<float>(quad_rate))},
        {"channel", gr::pmt::Value(static_cast<gr::Size_t>(soapy_channel))},
        {"center_frequency", gr::pmt::Value(soapy_freq)},
        {"bandwidth", gr::pmt::Value(soapy_bw)},
        {"gain", gr::pmt::Value(soapy_gain)},
        {"antenna", gr::pmt::Value(soapy_antenna)},
    }));

    auto& quad_demod = fg.emplaceBlock<gr::analog::QuadratureDemod<TR>>(
        make_props({{"gain", gr::pmt::Value(fm_demod_gain)}}));

    auto& deemph_filter = fg.emplaceBlock<gr::analog::FmDeemphasisFilter<TR>>(
        make_props({{"sample_rate", gr::pmt::Value(static_cast<float>(quad_rate))}, {"tau", gr::pmt::Value(75e-6f)}}));

    double stop_band_attenuation = 80.0;
    double rate = audio_rate / quad_rate;
    std::size_t num_filters = 32;

    auto taps_vec = gr::pfb::create_taps<TR>(rate, num_filters, stop_band_attenuation);
    auto taps_val = gr::pmt::Value(gr::Tensor<TR>(gr::data_from, taps_vec));
    auto& resampler = fg.emplaceBlock<gr::pfb::PfbArbResampler<TR>>(make_props({
        {"rate", gr::pmt::Value(rate)},
        {"taps", std::move(taps_val)},
        {"num_filters", gr::pmt::Value(num_filters)},
        {"stop_band_attenuation", gr::pmt::Value(stop_band_attenuation)},
    }));

    auto& volume_block = fg.emplaceBlock<gr::blocks::math::MultiplyConst<TR>>(make_props({
        {"name", gr::pmt::Value(std::string("volume"))},
        {"value", gr::pmt::Value(static_cast<TR>(volume))},
    }));

    auto& audio_sink = fg.emplaceBlock<gr::audio::RtAudioSink<TR>>(make_props({
        {"sample_rate", gr::pmt::Value(static_cast<float>(audio_rate))},
        {"channels_fallback", gr::pmt::Value(1)},
        {"device_index", gr::pmt::Value(-1)},
    }));

    if (audio_frames_per_buf > 0) {
        audio_sink.settings().setStaged(make_props({{"frames_per_buf", gr::pmt::Value(static_cast<uint32_t>(audio_frames_per_buf))}}));
    }
    if (audio_latency_s > 0.0) {
        audio_sink.settings().setStaged(make_props({{"target_latency_s", gr::pmt::Value(audio_latency_s)}}));
    }
    if (audio_ignore_tag_sample_rate) {
        audio_sink.settings().setStaged(make_props({{"ignore_tag_sample_rate", gr::pmt::Value(true)}}));
    }

    auto& audio_probe = fg.emplaceBlock<gr::basic::DataSink<TR>>(make_props({
        {"name", gr::pmt::Value(std::string("audio_probe"))},
        {"signal_name", gr::pmt::Value(std::string("audio"))},
        {"sample_rate", gr::pmt::Value(static_cast<float>(audio_rate))},
    }));

    const char* connection_error = "connection_error";

    if (fg.connect<"out">(soapy_rx).to<"in">(quad_demod) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(quad_demod).to<"in">(deemph_filter) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(deemph_filter).to<"in">(resampler) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(resampler).to<"in">(volume_block) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(volume_block).to<"in">(audio_sink) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }
    if (fg.connect<"out">(volume_block).to<"in">(audio_probe) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception(connection_error);
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto ret = sched.exchange(std::move(fg)); !ret) {
        throw std::runtime_error(std::format("failed to initialize scheduler: {}", ret.error()));
    }

    auto* soapy_model = find_block(sched, "soapy_rx");
    auto* volume_model = find_block(sched, "volume");

    if (!glfwInit()) {
        std::print("failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1200, 720, "fm_demodulator_imgui", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::print("failed to create window\n");
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    const char* glsl_version = "#version 330";
    ImGui_ImplOpenGL3_Init(glsl_version);

    gr::basic::PollerConfig poll_config;
    poll_config.overflowPolicy = gr::basic::OverflowPolicy::Drop;
    poll_config.minRequiredSamples = 64;
    poll_config.maxRequiredSamples = 1024;

    auto poller = gr::basic::globalDataSinkRegistry().getStreamingPoller<TR>(gr::basic::DataSinkQuery::signalName("audio"), poll_config);

    std::vector<float> plot_buffer;
    plot_buffer.reserve(8192);

    std::array<char, 64> freq_text{};
    std::array<char, 64> gain_text{};
    std::array<char, 64> volume_text{};
    std::snprintf(freq_text.data(), freq_text.size(), "%s", format_hz(soapy_freq).c_str());
    std::snprintf(gain_text.data(), gain_text.size(), "%.2f", soapy_gain);
    std::snprintf(volume_text.data(), volume_text.size(), "%.3f", volume);
    std::string status_line;

    std::jthread sched_thread([&sched]() {
        auto result = sched.runAndWait();
        if (!result.has_value()) {
            std::print("scheduler error: {}\n", result.error());
        }
    });

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (!poller) {
            poller = gr::basic::globalDataSinkRegistry().getStreamingPoller<TR>(gr::basic::DataSinkQuery::signalName("audio"), poll_config);
        }
        if (poller) {
            std::ignore = poller->process([&](std::span<const float> samples) {
                if (samples.empty()) {
                    return;
                }
                const std::size_t max_keep = 4096;
                for (float v : samples) {
                    plot_buffer.push_back(v);
                }
                if (plot_buffer.size() > max_keep) {
                    plot_buffer.erase(plot_buffer.begin(), plot_buffer.begin() + static_cast<std::ptrdiff_t>(plot_buffer.size() - max_keep));
                }
            });
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("FM Controls");
        ImGui::Text("Soapy controls (Hz / dB)");

        bool apply = false;
        if (ImGui::InputText("Frequency", freq_text.data(), freq_text.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            apply = true;
        }
        if (ImGui::InputText("Gain", gain_text.data(), gain_text.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            apply = true;
        }
        if (ImGui::InputText("Volume", volume_text.data(), volume_text.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            apply = true;
        }
        if (ImGui::Button("Apply")) {
            apply = true;
        }

        if (apply) {
            if (auto v = parse_with_suffix(freq_text.data())) {
                if (soapy_model) {
                    soapy_model->settings().setStaged(make_props({{"center_frequency", gr::pmt::Value(*v)}}));
                    status_line = std::format("freq updated -> {}", format_hz(*v));
                }
            } else {
                status_line = "invalid frequency";
            }
            if (auto v = parse_with_suffix(gain_text.data())) {
                if (soapy_model) {
                    soapy_model->settings().setStaged(make_props({{"gain", gr::pmt::Value(*v)}}));
                    status_line = std::format("gain updated -> {:.2f}", *v);
                }
            } else if (gain_text[0] != '\0') {
                status_line = "invalid gain";
            }
            if (auto v = parse_with_suffix(volume_text.data())) {
                if (volume_model) {
                    volume_model->settings().setStaged(make_props({{"value", gr::pmt::Value(static_cast<float>(*v))}}));
                    status_line = std::format("volume updated -> {:.3f}", *v);
                }
            } else if (volume_text[0] != '\0') {
                status_line = "invalid volume";
            }
        }

        if (!status_line.empty()) {
            ImGui::Text("%s", status_line.c_str());
        }

        if (soapy_model) {
            auto settings = soapy_model->settings().get();
            ImGui::Separator();
            ImGui::Text("Current freq: %s", format_variant(settings, "center_frequency").c_str());
            ImGui::Text("Current gain: %s", format_variant(settings, "gain").c_str());
        }
        if (volume_model) {
            auto settings = volume_model->settings().get();
            ImGui::Text("Current volume: %s", format_variant(settings, "value").c_str());
        }
        ImGui::End();

        ImGui::Begin("Audio Waveform");
        if (ImPlot::BeginPlot("Audio", ImVec2(-1, 300))) {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, static_cast<double>(plot_buffer.size()), ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -1.2, 1.2, ImGuiCond_Always);
            if (!plot_buffer.empty()) {
                ImPlot::PlotLine("audio", plot_buffer.data(), static_cast<int>(plot_buffer.size()));
            }
            ImPlot::EndPlot();
        }
        ImGui::End();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    std::ignore = sched.changeStateTo(gr::lifecycle::State::REQUESTED_STOP);
    sched_thread.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
