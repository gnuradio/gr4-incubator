#include <atomic>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/soapysdr/SoapyRx.hpp>

#include <CLI/CLI.hpp>

int main(int argc, char** argv) {
    CLI::App app{"Soapy capture test: read N samples and stop"};

    std::string driver;
    std::string args;
    double sample_rate = 400e3;
    double freq = 99.1e6;
    double bw = 400e3;
    double gain = 60.0;
    std::string antenna;
    std::size_t channel = 0;
    std::size_t  n_samples = 1'000'000;
    double       timeout_s = 0.0;
    std::uint32_t stream_timeout_us = 1'000;
    std::size_t  max_overflow = 10;

    app.add_option("--driver", driver, "Soapy driver (e.g., uhd, rtlsdr)");
    app.add_option("--args", args, "Soapy device args string");
    app.add_option("--rate", sample_rate, "Sample rate (Hz)");
    app.add_option("--freq", freq, "Center frequency (Hz)");
    app.add_option("--bw", bw, "Bandwidth (Hz)");
    app.add_option("--gain", gain, "Gain (dB)");
    app.add_option("--antenna", antenna, "Antenna name");
    app.add_option("--channel", channel, "RX channel index");
    app.add_option("-n,--samples", n_samples, "Number of samples to capture before stopping");
    app.add_option("--timeout", timeout_s, "Stop after N seconds (0 = disable)");
    app.add_option("--stream-timeout-us", stream_timeout_us, "Soapy stream timeout (us)");
    app.add_option("--max-overflow", max_overflow, "Max overflow count before error (0 = disable)");

    CLI11_PARSE(app, argc, argv);

    using T = std::complex<float>;

    gr::Graph fg;

    auto& source = fg.emplaceBlock<gr::soapysdr::SoapyRx<T>>({
        {"device", driver},
        {"device_args", args},
        {"sample_rate", static_cast<float>(sample_rate)},
        {"channel", static_cast<gr::Size_t>(channel)},
        {"center_frequency", freq},
        {"bandwidth", bw},
        {"gain", gain},
        {"antenna", antenna},
        {"stream_timeout_us", stream_timeout_us},
        {"max_overflow_count", static_cast<gr::Size_t>(max_overflow)},
    });

    auto& sink = fg.emplaceBlock<gr::testing::CountingSink<T>>({
        {"n_samples_max", static_cast<gr::Size_t>(n_samples)},
    });

    if (fg.connect<"out">(source).to<"in">(sink) != gr::ConnectionResult::SUCCESS) {
        throw gr::exception("connection_error");
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded> sched;
    if (auto ret = sched.exchange(std::move(fg)); !ret) {
        throw std::runtime_error(std::format("failed to initialize scheduler: {}", ret.error()));
    }

    std::atomic<bool> done{false};
    std::thread watchdog;
    if (timeout_s > 0.0) {
        watchdog = std::thread([&]() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
            while (!done.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!done.load(std::memory_order_relaxed)) {
                sink.requestStop();
            }
        });
    }

    const auto ret = sched.runAndWait();
    done.store(true, std::memory_order_relaxed);
    if (watchdog.joinable()) {
        watchdog.join();
    }
    if (!ret.has_value()) {
        std::cerr << "scheduler error: " << ret.error().message << "\n";
        return 1;
    }

    std::cout << "Captured samples: " << sink.count << "\n";
    return 0;
}
