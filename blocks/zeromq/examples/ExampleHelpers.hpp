#pragma once

#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

namespace gr::incubator::zeromq::examples {

template <typename T>
struct SequenceSource : gr::Block<SequenceSource<T>> {
    gr::PortOut<T> out;
    std::vector<T> values;
    std::size_t    index = 0;
    gr::Size_t     startup_delay_ms = 100;
    bool           repeat = false;
    bool           started = false;

    GR_MAKE_REFLECTABLE(SequenceSource, out, values, startup_delay_ms, repeat);

    [[nodiscard]] T processOne() {
        if (!started) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(startup_delay_ms));
        }
        if (values.empty()) {
            this->requestStop();
            return T{};
        }
        if (index >= values.size()) {
            if (!repeat) {
                this->requestStop();
                return T{};
            }
            index = 0;
        }
        return values[index++];
    }
};

template <typename T>
struct PrintingSink : gr::Block<PrintingSink<T>> {
    gr::PortIn<T> in;
    gr::Size_t    n_items_max = 16;
    std::size_t   count = 0;
    std::string   label = "recv";

    GR_MAKE_REFLECTABLE(PrintingSink, in, n_items_max, label);

    void processOne(const T& value) {
        std::cout << label << "[" << count << "] = " << value << '\n';
        ++count;
        if (n_items_max > 0 && count >= n_items_max) {
            this->requestStop();
        }
    }
};

template <typename SchedulerT>
void run_graph(gr::Graph&& graph) {
    SchedulerT scheduler;
    if (auto ret = scheduler.exchange(std::move(graph)); !ret) {
        throw std::runtime_error(std::format("failed to initialize scheduler: {}", ret.error()));
    }
    if (auto ret = scheduler.runAndWait(); !ret) {
        throw std::runtime_error(std::format("scheduler error: {}", ret.error()));
    }
}

inline void require_role(int argc, char** argv, std::string_view usage) {
    if (argc < 2) {
        throw std::runtime_error(std::string(usage));
    }
}

} // namespace gr::incubator::zeromq::examples
