#ifndef GNURADIO_INCUBATOR_SCHEDULER_BLOCKINGBACKOFF_HPP
#define GNURADIO_INCUBATOR_SCHEDULER_BLOCKINGBACKOFF_HPP

#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <format>
#include <iterator>
#include <memory>
#include <ranges>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

namespace gr::incubator::scheduler {

template<gr::scheduler::ExecutionPolicy execution = gr::scheduler::ExecutionPolicy::multiThreaded>
struct BlockingBackoff : gr::scheduler::SchedulerBase<BlockingBackoff<execution>, execution> {
    using Base        = gr::scheduler::SchedulerBase<BlockingBackoff<execution>, execution>;
    using Description = gr::Doc<R""(Scheduler prototype that backs off when a worker loop makes no stream progress.)"">;

    BlockingBackoff() : Base() {}
    explicit BlockingBackoff(gr::property_map initParameters) : Base(std::move(initParameters)) {}

    gr::Annotated<gr::Size_t, "initial_backoff_us", gr::Doc<"Initial sleep duration after no-progress worker cycles">>      initial_backoff_us = 50U;
    gr::Annotated<gr::Size_t, "max_backoff_us", gr::Doc<"Maximum sleep duration after repeated no-progress worker cycles">> max_backoff_us     = 2000U;
    gr::Annotated<gr::Size_t, "active_spin_count", gr::Doc<"No-progress worker cycles allowed before sleeping">>            active_spin_count  = 2U;

    GR_MAKE_REFLECTABLE(BlockingBackoff, initial_backoff_us, max_backoff_us, active_spin_count);

    void customInit() {
        [[maybe_unused]] const auto profilerEvent = this->_profilerHandler->startCompleteEvent("scheduler_blocking_backoff.init");

        const gr::Graph   flatGraph = gr::graph::flatten(*this->_graph);
        const std::size_t nBlocks   = flatGraph.blocks().size();

        std::size_t nBatches = 1UZ;
        if constexpr (execution == gr::scheduler::ExecutionPolicy::multiThreaded) {
            nBatches = std::max(1UZ, std::min(static_cast<std::size_t>(this->_pool->maxThreads()), nBlocks));
        }

        std::lock_guard lock(this->_executionOrderMutex);
        std::lock_guard guard(this->_adoptionBlocksMutex);
        this->_adoptionBlocks.clear();
        this->_adoptionBlocks.resize(nBatches);
        this->_executionOrder->clear();
        this->_executionOrder->reserve(nBatches);
        for (std::size_t batchIndex = 0UZ; batchIndex < nBatches; ++batchIndex) {
            std::vector<std::shared_ptr<gr::BlockModel>>& job = this->_executionOrder->emplace_back();
            job.reserve(nBlocks / nBatches + 1UZ);
            for (std::size_t blockIndex = batchIndex; blockIndex < nBlocks; blockIndex += nBatches) {
                job.push_back(flatGraph.blocks()[blockIndex]);
            }
        }
    }

    void poolWorker(const std::size_t runnerId, std::shared_ptr<gr::scheduler::JobLists> jobList) {
        using enum gr::lifecycle::State;

        std::shared_ptr<gr::Sequence> nRunningJobs = this->_nRunningJobs;

        nRunningJobs->incrementAndGet();
        nRunningJobs->notify_all();
        gr::thread_pool::thread::setThreadName(std::format("bb{}-{}", runnerId, gr::meta::shorten_type_name(this->unique_name)));

        [[maybe_unused]] auto profilerHandler = this->_profiler.forThisThread();

        std::vector<std::shared_ptr<gr::BlockModel>> localBlockList;
        {
            assert(jobList->size() > runnerId);
            std::lock_guard                                     lock(this->_executionOrderMutex);
            const std::vector<std::shared_ptr<gr::BlockModel>>& blocks = jobList->at(runnerId);
            localBlockList.reserve(blocks.size());
            std::ranges::copy(blocks, std::back_inserter(localBlockList));
        }

        std::size_t inactiveCycleCount = 0UZ;
        std::size_t backoffUs          = 0UZ;
        std::size_t messageRatioCount  = 0UZ;
        auto        activeState        = this->state();

        do {
            [[maybe_unused]] auto profilerEvent = profilerHandler->startCompleteEvent("scheduler_blocking_backoff.work");

            const bool hasMessagesToProcess = messageRatioCount == 0UZ || this->msgIn.available() > 0UZ || this->_fromChildMessagePort.available() > 0UZ;
            if (hasMessagesToProcess) {
                if (runnerId == 0UZ || nRunningJobs->value() == 0UZ) {
                    this->processScheduledMessages();
                }

                this->cleanupZombieBlocks(localBlockList);
                this->adoptBlocks(runnerId, localBlockList);

                std::ranges::for_each(localBlockList, &gr::BlockModel::processScheduledMessages);
                activeState = this->state();
                messageRatioCount++;
                inactiveCycleCount = 0UZ;
                backoffUs          = 0UZ;
            } else if (std::has_single_bit(this->process_stream_to_message_ratio.value)) {
                messageRatioCount = (messageRatioCount + 1U) & (this->process_stream_to_message_ratio.value - 1U);
            } else {
                messageRatioCount = (messageRatioCount + 1U) % this->process_stream_to_message_ratio.value;
            }

            if (activeState == RUNNING) {
                if (gr::atomic_ref(this->_workQuiescenceRequested).load_acquire()) {
                    std::this_thread::yield();
                } else {
                    gr::atomic_ref(this->_nWorkersInWork).fetch_add(1UZ);
                    if (gr::atomic_ref(this->_workQuiescenceRequested).load_acquire()) {
                        gr::atomic_ref(this->_nWorkersInWork).fetch_sub(1UZ);
                    } else {
                        const gr::work::Result result = this->traverseBlockListOnce(localBlockList);
                        gr::atomic_ref(this->_nWorkersInWork).fetch_sub(1UZ);

                        if (result.status == gr::work::Status::DONE) {
                            break;
                        }
                        if (result.status == gr::work::Status::ERROR) {
                            this->emitErrorMessageIfAny("LifecycleState (ERROR)", this->changeStateTo(ERROR));
                            break;
                        }

                        updateBackoff(result.performed_work, inactiveCycleCount, backoffUs);
                    }
                }
            } else if (activeState == PAUSED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(this->timeout_ms.value));
                messageRatioCount  = 0UZ;
                inactiveCycleCount = 0UZ;
                backoffUs          = 0UZ;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(this->timeout_ms.value));
                messageRatioCount  = 0UZ;
                inactiveCycleCount = 0UZ;
                backoffUs          = 0UZ;
            }

            if (backoffUs != 0UZ) {
                std::this_thread::sleep_for(std::chrono::microseconds(backoffUs));
            }

            activeState = this->state();
        } while (gr::lifecycle::isActive(activeState));

        std::ignore = nRunningJobs->subAndGet(1UZ);
        nRunningJobs->notify_all();
    }

private:
    void updateBackoff(std::size_t performedWork, std::size_t& inactiveCycleCount, std::size_t& backoffUs) const {
        if (performedWork != 0UZ) {
            inactiveCycleCount = 0UZ;
            backoffUs          = 0UZ;
            return;
        }

        inactiveCycleCount++;
        if (inactiveCycleCount <= active_spin_count.value) {
            backoffUs = 0UZ;
            return;
        }

        const std::size_t initialBackoffUs = static_cast<std::size_t>(initial_backoff_us.value);
        const std::size_t maxBackoffUs     = static_cast<std::size_t>(max_backoff_us.value);
        const std::size_t nextBackoffUs    = backoffUs == 0UZ ? initialBackoffUs : backoffUs * 2UZ;
        backoffUs                          = std::min(nextBackoffUs, maxBackoffUs);
    }
};

} // namespace gr::incubator::scheduler

#endif // GNURADIO_INCUBATOR_SCHEDULER_BLOCKINGBACKOFF_HPP
