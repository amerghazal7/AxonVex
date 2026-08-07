/**
 * @file systemPortRegistry.hpp
 * @brief Named system-port table + cross-system lock protocol
 * @author AxonVex Development Team
 *
 * Phase 2 core decomposition, migration step 1 (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.7). Extracted verbatim from AxonVexSystem's
 * systemInputPorts_/systemOutputPorts_/systemPortsMutex_/lockSystemPortsWith.
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axonvex::core {

class BasePort;
class ProcessingUnit;

/**
 * @brief Named system input/output port table for one AxonVexSystem.
 *
 * Pure ownership-free lookup table: stores non-owned BasePort* under a
 * caller-chosen name (stable-reference contract — ports are owned by the
 * ProcessingUnit that created them, which outlives its entries here; the
 * owning system removes a unit's entries via removeAllForOwner() before the
 * unit itself is destroyed).
 *
 * Threading contract: every method locks mutex_; no user code ever runs
 * under it. AxonVexSystem is a friend so its connectToSystem<T>/
 * disconnectFromSystem<T> templates can look up entries directly while
 * holding the pair of locks returned by lockWith() (re-locking mutex_ via a
 * public accessor while already holding it would be UB on a non-recursive
 * mutex).
 */
class SystemPortRegistry {
  public:
    SystemPortRegistry() = default;
    ~SystemPortRegistry() = default;

    // Holds a mutex_: non-copyable, non-movable (delete, never =default).
    SystemPortRegistry(const SystemPortRegistry&) = delete;
    SystemPortRegistry& operator=(const SystemPortRegistry&) = delete;
    SystemPortRegistry(SystemPortRegistry&&) = delete;
    SystemPortRegistry& operator=(SystemPortRegistry&&) = delete;

    /// Refuses (returns false) if `name` is already assigned as an input.
    bool assignInput(const std::string& name, BasePort* port);
    /// Refuses (returns false) if `name` is already assigned as an output.
    bool assignOutput(const std::string& name, BasePort* port);

    bool removeInput(const std::string& name);
    bool removeOutput(const std::string& name);

    BasePort* input(const std::string& name) const;
    BasePort* output(const std::string& name) const;

    bool hasInput(const std::string& name) const;
    bool hasOutput(const std::string& name) const;

    std::vector<std::string> inputNames() const;
    std::vector<std::string> outputNames() const;

    size_t inputCount() const noexcept;
    size_t outputCount() const noexcept;

    /// One port's name plus the (owned, copied) fields a debug report wants.
    /// Populated entirely under mutex_ so no BasePort* it read is ever
    /// carried past the lock — see describeInputs()/describeOutputs().
    struct PortDescription {
        std::string name;
        std::string dataTypeName;
        std::string ownerName;
    };

    /// Snapshots every input port's name + getDataTypeName() +
    /// getOwner()->getName() in one critical section. Fixes the
    /// getSystemPortInfo() use-after-free: the old inputNames() -> input()
    /// -> dereference sequence released mutex_ between the lookup and the
    /// dereference, leaving a window for removeAllForOwner() + unit
    /// destruction to free the port first. getDataTypeName() and
    /// getOwner() are plain non-locking, non-reentrant accessors (no user
    /// callbacks), so calling them while already holding mutex_ is safe.
    std::vector<PortDescription> describeInputs() const;
    std::vector<PortDescription> describeOutputs() const;

    /// Clears both tables (reset()/cleanupComponents() path).
    void clear();

    struct RemovedPorts {
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
    };

    /// Removes every input/output entry whose port's getOwner() == owner.
    /// Returns the removed names so the caller can log/publish events for
    /// them (façade orchestrates side effects; the registry has none).
    /// Callable while the caller already holds an unrelated lock of its own
    /// (e.g. AxonVexSystem's unitsMutex_) — this only ever takes its own
    /// mutex_, so no new lock-order pairing is introduced.
    RemovedPorts removeAllForOwner(ProcessingUnit* owner);

    /**
     * C9 — inherited verbatim from AxonVexSystem::lockSystemPortsWith: locks
     * this and target's mutex_ without a lock-order deadlock (std::lock),
     * and locks only once when target == this (locking a non-recursive
     * mutex twice is UB; the second lock is empty in the self case).
     */
    std::pair<std::unique_lock<std::mutex>, std::unique_lock<std::mutex>> lockWith(
        SystemPortRegistry& target);

  private:
    friend class AxonVexSystem;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, BasePort*> inputs_;
    std::unordered_map<std::string, BasePort*> outputs_;
};

} // namespace axonvex::core
