#include <boost/ut.hpp>

#include <format>
#include <tuple>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/scheduler/BlockingBackoff.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

const boost::ut::suite<"BlockingBackoff"> BlockingBackoffTests = [] {
    using namespace boost::ut;

    "finite graph completes"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::testing::CountingSource<float>>(gr::property_map{{"n_samples_max", gr::Size_t(32)}});
        auto&     copy   = graph.emplaceBlock<gr::testing::Copy<float>>();
        auto&     sink   = graph.emplaceBlock<gr::testing::CountingSink<float>>(gr::property_map{{"n_samples_max", gr::Size_t(32)}});

        expect(graph.connect<"out", "in">(source, copy).has_value());
        expect(graph.connect<"out", "in">(copy, sink).has_value());

        gr::incubator::scheduler::BlockingBackoff<> scheduler;
        if (auto result = scheduler.exchange(std::move(graph)); !result) {
            expect(false) << std::format("could not initialise scheduler: {}", result.error()) << fatal;
        }

        expect(scheduler.runAndWait().has_value());
        expect(eq(sink.count.value, gr::Size_t(32)));
    };

    "settings are reflected"_test = [] {
        gr::incubator::scheduler::BlockingBackoff<> scheduler;

        const auto errors = scheduler.settings().set({
            {"initial_backoff_us", gr::Size_t(25)},
            {"max_backoff_us", gr::Size_t(400)},
            {"active_spin_count", gr::Size_t(1)},
        });
        expect(errors.empty());
        std::ignore = scheduler.settings().activateContext();
        std::ignore = scheduler.settings().applyStagedParameters();

        expect(eq(scheduler.initial_backoff_us.value, gr::Size_t(25)));
        expect(eq(scheduler.max_backoff_us.value, gr::Size_t(400)));
        expect(eq(scheduler.active_spin_count.value, gr::Size_t(1)));
    };

    "multi threaded plan creates more than one job list"_test = [] {
        gr::Graph graph;
        auto&     sourceA = graph.emplaceBlock<gr::testing::CountingSource<float>>(gr::property_map{{"n_samples_max", gr::Size_t(8)}});
        auto&     sinkA   = graph.emplaceBlock<gr::testing::CountingSink<float>>(gr::property_map{{"n_samples_max", gr::Size_t(8)}});
        auto&     sourceB = graph.emplaceBlock<gr::testing::CountingSource<float>>(gr::property_map{{"n_samples_max", gr::Size_t(8)}});
        auto&     sinkB   = graph.emplaceBlock<gr::testing::CountingSink<float>>(gr::property_map{{"n_samples_max", gr::Size_t(8)}});

        expect(graph.connect<"out", "in">(sourceA, sinkA).has_value());
        expect(graph.connect<"out", "in">(sourceB, sinkB).has_value());

        gr::incubator::scheduler::BlockingBackoff<> scheduler;
        if (auto result = scheduler.exchange(std::move(graph)); !result) {
            expect(false) << std::format("could not initialise scheduler: {}", result.error()) << fatal;
        }

        std::ignore = scheduler.changeStateTo(gr::lifecycle::State::INITIALISED);
        expect(scheduler.jobs()->size() > 1UZ);
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
