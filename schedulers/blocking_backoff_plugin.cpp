#include <gnuradio-4.0/Plugin.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/scheduler/BlockingBackoff.hpp>

GR_PLUGIN("Gr4IncubatorSchedulers", "gr4-incubator", "GPL-3.0-or-later", "0.1")

static const bool registerBlockingBackoffSchedulers = [] {
    auto& registry = static_cast<gr::SchedulerRegistry&>(grPluginInstance());

    registry.template insert<gr::incubator::scheduler::BlockingBackoff<gr::scheduler::ExecutionPolicy::multiThreaded>>("=gr::incubator::scheduler::BlockingBackoff");
    registry.template insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>("=gr::scheduler::SimpleSingle");
    registry.template insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>>("=gr::scheduler::SimpleMulti");

    return true;
}();
