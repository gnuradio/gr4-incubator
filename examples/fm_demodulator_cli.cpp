#include <atomic>
#include <cctype>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <limits>
#include <cmath>
#include <mutex>
#include <memory_resource>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/Converters.hpp>
#include <gnuradio-4.0/fileio/BasicFileIo.hpp>
#include <gnuradio-4.0/analog/QuadratureDemod.hpp>
#include <gnuradio-4.0/analog/FmDeemphasisFilter.hpp>
#include <gnuradio-4.0/audio/RtAudioSink.hpp>
#include <gnuradio-4.0/math/Math.hpp>
#include <gnuradio-4.0/pfb/PfbArbResampler.hpp>
#include <gnuradio-4.0/pfb/PfbArbResamplerTaps.hpp>
#include <gnuradio-4.0/soapysdr/SoapyRx.hpp>

#include <CLI/CLI.hpp>

#include <fcntl.h>
#include <ncurses.h>
#include <unistd.h>

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

std::string format_value(const gr::pmt::Value& v) {
    if (v.is_string()) {
        return std::string(v.value_or(std::string_view{}));
    }
    if (auto f = v.get_if<float>()) {
        return std::format("{}", *f);
    }
    if (auto d = v.get_if<double>()) {
        return std::format("{}", *d);
    }
    if (auto u = v.get_if<uint64_t>()) {
        return std::format("{}", *u);
    }
    if (auto i = v.get_if<int64_t>()) {
        return std::format("{}", *i);
    }
    if (auto b = v.get_if<bool>()) {
        return *b ? "true" : "false";
    }
    return "<unsupported>";
}

float parse_freq_hz(const std::string& token) {
    if (token.empty()) {
        throw std::runtime_error("empty frequency");
    }
    char suffix = token.back();
    double mult = 1.0;
    std::string number = token;
    if (suffix == 'k' || suffix == 'K') {
        mult = 1e3;
        number.pop_back();
    } else if (suffix == 'm' || suffix == 'M') {
        mult = 1e6;
        number.pop_back();
    } else if (suffix == 'g' || suffix == 'G') {
        mult = 1e9;
        number.pop_back();
    }
    return static_cast<float>(std::stod(number) * mult);
}

void trim_inplace(std::string& s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}
} // namespace

int main(int argc, char** argv) {
    CLI::App app{"FM demodulator with CLI control"};

    std::string source_type = "soapy";
    std::string filename;
    double      quad_rate = 400e3;
    std::string soapy_driver = "uhd";
    std::string soapy_args;
    double      soapy_freq = 96e6;
    double      soapy_bw = 200e3;
    double      soapy_gain = 10.0;
    std::string soapy_antenna;
    std::size_t soapy_channel = 0;
    bool        soapy_debug = false;
    float       volume = 0.5f;
    std::size_t watchdog_timeout_ms = 0;
    std::size_t watchdog_inactive_count = 0;

    app.add_option("--source", source_type, "Input source: file|soapy")
        ->check(CLI::IsMember({"file", "soapy"}));
    app.add_option("-f,--file", filename, "Input file (fc32)");
    app.add_option("-r,--rate", quad_rate, "Input sample rate (Hz)");
    app.add_option("--soapy-driver", soapy_driver, "SoapySDR driver");
    app.add_option("--soapy-args", soapy_args, "SoapySDR device args");
    app.add_option("--soapy-freq", soapy_freq, "SoapySDR center frequency (Hz)");
    app.add_option("--soapy-bw", soapy_bw, "SoapySDR bandwidth (Hz)");
    app.add_option("--soapy-gain", soapy_gain, "SoapySDR gain (dB)");
    app.add_option("--soapy-antenna", soapy_antenna, "SoapySDR antenna name");
    app.add_option("--soapy-channel", soapy_channel, "SoapySDR RX channel index");
    app.add_flag("--soapy-debug", soapy_debug, "Enable SoapyRx debug logging");
    app.add_option("--volume", volume, "Audio volume scalar (0..1)");
    app.add_option("--watchdog-timeout-ms", watchdog_timeout_ms, "Scheduler watchdog timeout (ms, 0 disables)");
    app.add_option("--watchdog-inactive-count", watchdog_inactive_count, "Scheduler watchdog inactive count");

    CLI11_PARSE(app, argc, argv);

    using TR = float;
    using T = std::complex<TR>;

    Graph fg;

    auto& quad_demod = fg.emplaceBlock<gr::analog::QuadratureDemod<TR>>(
        make_props({{"gain", gr::pmt::Value(quad_rate / (2 * M_PI * 75e3))}}));
    auto& deemph_filter = fg.emplaceBlock<gr::analog::FmDeemphasisFilter<TR>>(
        make_props({{"sample_rate", gr::pmt::Value(static_cast<float>(quad_rate))}, {"tau", gr::pmt::Value(75e-6f)}}));

    double stop_band_attenuation = 80.0;
    double rate = 32e3 / quad_rate;
    size_t num_filters = 32;
    auto taps_vec = gr::pfb::create_taps<TR>(rate, num_filters, stop_band_attenuation);
    auto taps_val = gr::pmt::Value(gr::Tensor<TR>(gr::data_from, taps_vec));
    auto& resampler = fg.emplaceBlock<gr::pfb::PfbArbResampler<TR>>(make_props({
        {"rate", gr::pmt::Value(rate)},
        {"taps", std::move(taps_val)},
        {"num_filters", gr::pmt::Value(num_filters)},
        {"stop_band_attenuation", gr::pmt::Value(stop_band_attenuation)},
    }));

    constexpr std::string_view kSoapyName = "soapy_rx";
    constexpr std::string_view kVolumeName = "volume";

    auto& volume_block = fg.emplaceBlock<gr::blocks::math::MultiplyConst<TR>>(
        make_props({{"value", gr::pmt::Value(volume)}, {"name", gr::pmt::Value(std::string(kVolumeName))}}));

    auto& audio_sink = fg.emplaceBlock<gr::audio::RtAudioSink<TR>>(make_props({
        {"sample_rate", gr::pmt::Value(32000)},
        {"channels_fallback", gr::pmt::Value(1)},
        {"device_index", gr::pmt::Value(-1)},
    }));

    const char* connection_error = "connection_error";

    if (source_type == "file") {
        if (filename.empty()) {
            throw std::runtime_error("source=file requires --file");
        }
        auto& source = fg.emplaceBlock<gr::blocks::fileio::BasicFileSource<T>>(make_props({
            {"file_name", gr::pmt::Value(filename)},
            {"repeat", gr::pmt::Value(true)},
            {"disconnect_on_done", gr::pmt::Value(false)},
        }));
        if (fg.connect<"out">(source).to<"in">(quad_demod) != gr::ConnectionResult::SUCCESS) {
            throw gr::exception(connection_error);
        }
    } else {
        auto& source = fg.emplaceBlock<gr::soapysdr::SoapyRx<T>>(make_props({
            {"device", gr::pmt::Value(soapy_driver)},
            {"device_args", gr::pmt::Value(soapy_args)},
            {"sample_rate", gr::pmt::Value(static_cast<float>(quad_rate))},
            {"channel", gr::pmt::Value(static_cast<gr::Size_t>(soapy_channel))},
            {"center_frequency", gr::pmt::Value(soapy_freq)},
            {"bandwidth", gr::pmt::Value(soapy_bw)},
            {"gain", gr::pmt::Value(soapy_gain)},
            {"antenna", gr::pmt::Value(soapy_antenna)},
            {"debug", gr::pmt::Value(soapy_debug)},
            {"name", gr::pmt::Value(std::string(kSoapyName))},
        }));
        if (fg.connect<"out">(source).to<"in">(quad_demod) != gr::ConnectionResult::SUCCESS) {
            throw gr::exception(connection_error);
        }
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

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded> sched;
    if (auto ret = sched.exchange(std::move(fg)); !ret) {
        throw std::runtime_error(std::format("failed to initialize scheduler: {}", ret.error()));
    }
    if (watchdog_timeout_ms == 0) {
        sched.settings().set(make_props({
            {"watchdog_timeout", gr::pmt::Value(static_cast<gr::Size_t>(60'000))},
            {"timeout_inactivity_count", gr::pmt::Value(std::numeric_limits<gr::Size_t>::max())},
        }));
    } else {
        sched.settings().set(make_props({
            {"watchdog_timeout", gr::pmt::Value(static_cast<gr::Size_t>(watchdog_timeout_ms))},
            {"timeout_inactivity_count", gr::pmt::Value(static_cast<gr::Size_t>(watchdog_inactive_count))},
        }));
    }

    auto find_block = [&](std::string_view name) -> std::shared_ptr<gr::BlockModel> {
        std::shared_ptr<gr::BlockModel> found;
        gr::graph::forEachBlock<gr::block::Category::All>(sched.graph(), [&](const std::shared_ptr<gr::BlockModel>& block) {
            if (block->name() == name || block->uniqueName() == name) {
                found = block;
            }
        });
        return found;
    };

    auto soapy_block = find_block(kSoapyName);
    auto volume_block_model = find_block(kVolumeName);

    int pipefd[2]{};
    if (pipe(pipefd) != 0) {
        throw std::runtime_error("pipe() failed");
    }
    int tty_in_fd = open("/dev/tty", O_RDONLY);
    int tty_out_fd = open("/dev/tty", O_WRONLY);
    if (tty_in_fd < 0 || tty_out_fd < 0) {
        throw std::runtime_error("failed to open /dev/tty");
    }

    FILE* tty_in = fdopen(tty_in_fd, "r");
    FILE* tty_out = fdopen(tty_out_fd, "w");
    if (!tty_in || !tty_out) {
        throw std::runtime_error("fdopen(/dev/tty) failed");
    }

    SCREEN* screen = newterm(nullptr, tty_out, tty_in);
    if (!screen) {
        throw std::runtime_error("newterm failed");
    }
    set_term(screen);
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) {
        endwin();
        throw std::runtime_error("dup2 failed");
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    close(pipefd[1]);

    std::atomic<bool> running{true};
    std::mutex        log_mu;
    std::deque<std::string> log_lines;
    constexpr std::size_t kMaxLogLines = 2000;

    std::mutex        result_mu;
    std::optional<gr::Error> sched_error;

    std::thread output_thread([&] {
        std::string buffer;
        std::vector<char> chunk(1024);
        while (running.load()) {
            ssize_t n = read(pipefd[0], chunk.data(), chunk.size());
            if (n <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            buffer.append(chunk.data(), static_cast<std::size_t>(n));
            std::size_t pos = 0;
            while (true) {
                auto nl = buffer.find('\n', pos);
                if (nl == std::string::npos) {
                    buffer.erase(0, pos);
                    break;
                }
                std::string line = buffer.substr(pos, nl - pos);
                pos = nl + 1;
                std::lock_guard lk(log_mu);
                log_lines.push_back(std::move(line));
                if (log_lines.size() > kMaxLogLines) {
                    log_lines.pop_front();
                }
            }
        }
    });

    std::thread sched_thread([&] {
        auto result = sched.runAndWait();
        if (!result.has_value()) {
            std::lock_guard lk(result_mu);
            sched_error = result.error();
        }
        running.store(false);
    });

    std::string input_line;
    std::string status_line = "Type: set freq 99.1M | set gain 22 | set volume 60 | exit";

    auto render = [&] {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        const int input_row = rows - 1;
        const int status_row = rows - 2;
        const int log_rows = rows - 2;

        erase();
        std::deque<std::string> snapshot;
        {
            std::lock_guard lk(log_mu);
            snapshot = log_lines;
        }
        int start = std::max(0, static_cast<int>(snapshot.size()) - log_rows);
        for (int i = 0; i < log_rows; ++i) {
            int idx = start + i;
            if (idx >= static_cast<int>(snapshot.size())) break;
            mvaddnstr(i, 0, snapshot[idx].c_str(), cols - 1);
        }

        mvaddnstr(status_row, 0, status_line.c_str(), cols - 1);
        std::string prompt = ">> " + input_line;
        mvaddnstr(input_row, 0, prompt.c_str(), cols - 1);
        move(input_row, static_cast<int>(prompt.size()));
        refresh();
    };

    render();
    while (running.load()) {
        int ch = getch();
        if (ch == ERR) {
            render();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            std::string line = input_line;
            input_line.clear();
            trim_inplace(line);
            if (!line.empty()) {
                if (line == "quit" || line == "exit") {
                    running.store(false);
                    break;
                }
                std::istringstream iss(line);
                std::string cmd;
                iss >> cmd;
                std::string key;
                std::string value;
                if (cmd == "get") {
                    iss >> key;
                    if (key.empty()) {
                        status_line = "usage: get <param>";
                        render();
                        continue;
                    }
                    if (key == "freq") {
                        if (!soapy_block) {
                            status_line = "freq get ignored (source!=soapy)";
                        } else {
                            if (auto v = soapy_block->settings().get("center_frequency")) {
                                status_line = "freq = " + format_value(*v);
                            } else {
                                status_line = "freq unavailable";
                            }
                        }
                    } else if (key == "gain") {
                        if (!soapy_block) {
                            status_line = "gain get ignored (source!=soapy)";
                        } else {
                            if (auto v = soapy_block->settings().get("gain")) {
                                status_line = "gain = " + format_value(*v);
                            } else {
                                status_line = "gain unavailable";
                            }
                        }
                    } else if (key == "volume") {
                        if (!volume_block_model) {
                            status_line = "volume get failed (block missing)";
                        } else {
                            if (auto v = volume_block_model->settings().get("value")) {
                                status_line = "volume = " + format_value(*v);
                            } else {
                                status_line = "volume unavailable";
                            }
                        }
                    } else {
                        status_line = "unknown param";
                    }
                    render();
                    continue;
                }
                if (cmd != "set") {
                    status_line = "unknown command";
                    render();
                    continue;
                }
                iss >> key >> value;
                if (key.empty() || value.empty()) {
                    status_line = "usage: set <param> <value>";
                    render();
                    continue;
                }
                try {
                    if (key == "freq") {
                        if (!soapy_block) {
                            status_line = "freq set ignored (source!=soapy)";
                            render();
                            continue;
                        }
                        float hz = parse_freq_hz(value);
                        auto failed = soapy_block->settings().setStaged(
                            make_props({{"center_frequency", gr::pmt::Value(static_cast<double>(hz))}}));
                        status_line = failed.empty() ? "freq staged" : "failed to stage freq";
                    } else if (key == "gain") {
                        if (!soapy_block) {
                            status_line = "gain set ignored (source!=soapy)";
                            render();
                            continue;
                        }
                        float g = std::stof(value);
                        auto failed = soapy_block->settings().setStaged(
                            make_props({{"gain", gr::pmt::Value(static_cast<double>(g))}}));
                        status_line = failed.empty() ? "gain staged" : "failed to stage gain";
                    } else if (key == "volume") {
                        float v = std::stof(value);
                        if (v > 1.0f) {
                            v *= 0.01f;
                        }
                        if (v < 0.0f) v = 0.0f;
                        if (v > 1.0f) v = 1.0f;
                        if (!volume_block_model) {
                            status_line = "volume set failed (block missing)";
                            render();
                            continue;
                        }
                        auto failed = volume_block_model->settings().setStaged(
                            make_props({{"value", gr::pmt::Value(v)}}));
                        status_line = failed.empty() ? "volume staged" : "failed to stage volume";
                    } else {
                        status_line = "unknown param";
                    }
                } catch (const std::exception& e) {
                    status_line = std::string("error: ") + e.what();
                }
            }
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!input_line.empty()) {
                input_line.pop_back();
            }
        } else if (std::isprint(ch)) {
            input_line.push_back(static_cast<char>(ch));
        }
        render();
    }

    volume_block.requestStop();
    if (sched_thread.joinable()) {
        sched_thread.join();
    }
    running.store(false);
    if (output_thread.joinable()) {
        output_thread.join();
    }

    endwin();

    if (sched_error.has_value()) {
        std::cerr << "scheduler error: " << sched_error->message << "\n";
        return 1;
    }

    return 0;
}
