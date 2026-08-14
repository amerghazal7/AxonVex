#include <axonvex_core/lifecycleController.hpp>

namespace axonvex::core {

LifecycleController::LifecycleController(StateChangeHook onTransitioned,
                                         StateChangeHook onInvalidTransition,
                                         Predicate isOnEventDispatchThread,
                                         Predicate isOnHealthMonitorThread,
                                         Predicate isOnSchedulerThread)
    : onTransitioned_(std::move(onTransitioned)),
      onInvalidTransition_(std::move(onInvalidTransition)),
      isOnEventDispatchThread_(std::move(isOnEventDispatchThread)),
      isOnHealthMonitorThread_(std::move(isOnHealthMonitorThread)),
      isOnSchedulerThread_(std::move(isOnSchedulerThread)) {}

bool LifecycleController::transition(SystemState newState) {
    std::lock_guard<std::mutex> lock(stateMutex_);

    SystemState currentState = currentState_.load();

    // C7: an emergency shutdown is legal from every state — FATAL_ERROR is the
    // one transition that must never be refused.
    bool validTransition = false;
    if (newState == SystemState::FATAL_ERROR) {
        validTransition = true;
    } else {
        switch (currentState) {
            case SystemState::UNINITIALIZED:
                validTransition = (newState == SystemState::INITIALIZING);
                break;
            case SystemState::INITIALIZING:
                validTransition =
                    (newState == SystemState::INITIALIZED || newState == SystemState::ERROR);
                break;
            case SystemState::INITIALIZED:
                validTransition = (newState == SystemState::STARTING);
                break;
            case SystemState::STARTING:
                validTransition =
                    (newState == SystemState::RUNNING || newState == SystemState::ERROR);
                break;
            case SystemState::RUNNING:
                validTransition =
                    (newState == SystemState::PAUSING || newState == SystemState::STOPPING ||
                     newState == SystemState::ERROR);
                break;
            case SystemState::PAUSING:
                validTransition =
                    (newState == SystemState::PAUSED || newState == SystemState::ERROR);
                break;
            case SystemState::PAUSED:
                validTransition =
                    (newState == SystemState::RESUMING || newState == SystemState::STOPPING ||
                     newState == SystemState::ERROR);
                break;
            case SystemState::RESUMING:
                validTransition =
                    (newState == SystemState::RUNNING || newState == SystemState::ERROR);
                break;
            case SystemState::STOPPING:
                validTransition = (newState == SystemState::STOPPED);
                break;
            case SystemState::STOPPED:
                validTransition =
                    (newState == SystemState::STARTING || newState == SystemState::UNINITIALIZED);
                break;
            case SystemState::ERROR:
                validTransition = true; // Can transition to any state from error
                break;
            case SystemState::FATAL_ERROR:
                validTransition = (newState == SystemState::UNINITIALIZED);
                break;
        }
    }

    if (!validTransition) {
        if (onInvalidTransition_) {
            onInvalidTransition_(currentState, newState);
        }
        return false;
    }

    currentState_.store(newState);

    if (onTransitioned_) {
        onTransitioned_(currentState, newState);
    }

    return true;
}

SystemState LifecycleController::state() const noexcept {
    return currentState_.load();
}

void LifecycleController::clearShutdownLatch() noexcept {
    isShuttingDown_.store(false);
}

void LifecycleController::requestShutdown() noexcept {
    isShuttingDown_.store(true);
}

bool LifecycleController::isShuttingDown() const noexcept {
    return isShuttingDown_.load();
}

bool LifecycleController::shutdownRequested() const noexcept {
    return isShuttingDown_.load() || currentState_.load() >= SystemState::STOPPING;
}

bool LifecycleController::isOnWorkerThread() const noexcept {
    return isOnEventDispatchThread_() || isOnHealthMonitorThread_() || isOnSchedulerThread_();
}

std::unique_lock<std::mutex> LifecycleController::acquireTeardown() {
    return std::unique_lock<std::mutex>(shutdownMutex_);
}

std::unique_lock<std::mutex> LifecycleController::tryAcquireTeardown() {
    return std::unique_lock<std::mutex>(shutdownMutex_, std::try_to_lock);
}

} // namespace axonvex::core
