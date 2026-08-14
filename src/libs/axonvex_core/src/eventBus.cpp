#include <axonvex_core/eventBus.hpp>
#include <axonvex_core/logger.hpp>
#include <chrono>
#include <string>

namespace axonvex::core {

EventBus::EventBus(LoggerAccessor logger, Predicate isShuttingDown, Predicate shutdownRequested,
                   std::function<void()> onEventDropped)
    : logger_(logger), isShuttingDown_(std::move(isShuttingDown)),
      shutdownRequested_(std::move(shutdownRequested)), onEventDropped_(std::move(onEventDropped)) {
}

void EventBus::reinitialize(size_t poolSize, size_t queueSize) {
    eventPool_ = std::make_unique<MemoryPool<SystemEvent>>(poolSize);
    eventQueue_ = std::make_unique<ThreadSafeQueue<SystemEvent*>>(queueSize);
}

void EventBus::publish(const SystemEvent& event) {
    // Components may not exist yet (e-stop on a never-initialized system).
    if (!eventPool_ || !eventQueue_) {
        return;
    }
    // C25: once shutdown begins the dispatch thread stops draining the
    // queue -- allocating here would leak. Drop shutdown-time events.
    if (isShuttingDown_()) {
        return;
    }

    SystemEvent* eventPtr = eventPool_->allocateObject(event);

    if (eventPtr) {
        if (!eventQueue_->enqueue(eventPtr)) {
            eventPool_->deallocateObject(eventPtr);
            if (Logger* logger = logger_()) {
                logger->warning("EventBus", "Event queue overflow, event dropped.");
            }
            if (onEventDropped_) {
                onEventDropped_();
            }
        }
    } else {
        if (Logger* logger = logger_()) {
            logger->warning("EventBus", "Event pool exhausted, event dropped.");
        }
        if (onEventDropped_) {
            onEventDropped_();
        }
    }
}

uint32_t EventBus::registerCallback(EventCallback cb) {
    if (!cb) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(callbacksMutex_);
    uint32_t id = nextCallbackId_.fetch_add(1);

    if (callbacks_.size() <= id) {
        callbacks_.resize(id + 1);
    }
    callbacks_[id] = std::move(cb);

    return id;
}

void EventBus::unregisterCallback(uint32_t id) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);

    if (id < callbacks_.size()) {
        callbacks_[id] = nullptr;
    }
}

void EventBus::resetCallbacks() noexcept {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    callbacks_.clear();
    nextCallbackId_.store(1);
}

void EventBus::start() {
    // C46-shape: running_ is flipped inside WorkerThread::start()'s
    // beforeSpawn hook, strictly after the stale-handle join and strictly
    // before the new thread exists -- see workerThread.hpp for why that
    // ordering is load-bearing.
    dispatchThread_.start([this] { dispatchLoop(); }, [this] { running_.store(true); });
}

bool EventBus::join() {
    return dispatchThread_.join();
}

void EventBus::requestStop() noexcept {
    running_.store(false);
}

bool EventBus::stopAndJoin() {
    requestStop();
    bool joined = dispatchThread_.join();
    if (joined) {
        drain();
    }
    return joined;
}

void EventBus::drain() noexcept {
    // C25: events still queued when the dispatch thread exits would leak
    // their strings; return them to the pool without dispatching.
    if (!eventQueue_ || !eventPool_) {
        return;
    }
    while (true) {
        auto eventOpt = eventQueue_->tryDequeue(std::chrono::milliseconds(0));
        if (!eventOpt.has_value()) {
            break;
        }
        eventPool_->deallocateObject(eventOpt.value());
    }
}

bool EventBus::isOnDispatchThread() const noexcept {
    return dispatchThread_.isOnThisThread();
}

void EventBus::dispatchLoop() {
    // C41: the thread-id publish (first act) / clear (last, every exit
    // path) happens automatically inside detail::WorkerThread::start() --
    // isOnDispatchThread() reads it via dispatchThread_.isOnThisThread().

    while (running_.load() && !shutdownRequested_()) {
        // Dequeue an event with a timeout to allow for graceful shutdown.
        auto eventOpt = eventQueue_->tryDequeue(std::chrono::milliseconds(100));

        if (!eventOpt.has_value()) {
            continue;
        }

        SystemEvent* eventPtr = eventOpt.value();

        // Snapshot under the lock, invoke outside it -- C18: user callbacks
        // must never run while callbacksMutex_ is held.
        std::vector<EventCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(callbacksMutex_);
            callbacks = callbacks_;
        }
        for (const auto& callback : callbacks) {
            if (callback) {
                try {
                    callback(*eventPtr);
                } catch (const std::exception& e) {
                    if (Logger* logger = logger_()) {
                        logger->warning("EventBus",
                                        "Event callback failed: " + std::string(e.what()));
                    }
                }
            }
        }

        eventPool_->deallocateObject(eventPtr);
    }
}

} // namespace axonvex::core
