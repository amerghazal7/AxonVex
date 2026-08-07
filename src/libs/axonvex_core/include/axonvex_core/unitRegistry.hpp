/**
 * @file unitRegistry.hpp
 * @brief ProcessingUnit ownership + id lookup table (no orchestration)
 * @author AxonVex Development Team
 *
 * Phase 2 core decomposition, migration step 2 (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.6). Extracted from AxonVexSystem's
 * processingUnits_/unitToIdMap_/unitsMutex_/nextUnitId_.
 */

#pragma once

#include <axonvex_core/utils/optional.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace axonvex::core {

class ProcessingUnit;

/**
 * @brief Owns every registered ProcessingUnit and maps it to/from a
 * generated id.
 *
 * Pure ownership + lookup: NO scheduling, NO events, NO logging. Orchestration
 * (schedule-if-running, PROCESSING_UNIT_ADDED event, rollback-on-schedule-
 * failure) stays in the façade's registerProcessingUnit, which composes this
 * registry with timing and events. Rationale: keeps the registry dependency-
 * free and makes bulk-add (loadFromSpec) trivially testable.
 *
 * Threading contract: every method locks mutex_; no unit destructor ever
 * runs under it — remove()/clear() return ownership so the caller destroys
 * units OUTSIDE this lock: unit destructors can run user code via
 * finalize(), so destroying under mutex_ would itself be a latent C18-shape
 * bug (the previous AxonVexSystem::unregisterProcessingUnit erased the
 * unique_ptr, and therefore ran ~ProcessingUnit(), while still holding
 * unitsMutex_ — this extraction fixes that for free).
 *
 * addAndRun()/forEach() are the one deliberate exception to "nothing runs
 * under mutex_": they exist because a caller that only gets a raw pointer
 * back from add()/all() has no way to keep it valid once mutex_ is
 * released — a concurrent clear()/remove() can free it before the caller
 * finishes using it (this is exactly what happened pre-fix in
 * AxonVexSystem::registerProcessingUnit/startComponents, an ASan-confirmed
 * heap-use-after-free). Their callback runs under mutex_ so the caller's
 * "use the pointer" span is atomic with the insert/iteration; it is
 * synchronous, caller-local code, never a long-lived or externally
 * registered handler, so it is not the C18 class of bug (arbitrary
 * user/plugin callbacks invoked while holding a lock) — but it must never
 * call back into this UnitRegistry (mutex_ is not recursive) and must not
 * block or do I/O.
 */
class UnitRegistry {
  public:
    explicit UnitRegistry(size_t maxUnits);
    ~UnitRegistry() = default;

    // Holds a mutex_: non-copyable, non-movable (delete, never =default).
    UnitRegistry(const UnitRegistry&) = delete;
    UnitRegistry& operator=(const UnitRegistry&) = delete;
    UnitRegistry(UnitRegistry&&) = delete;
    UnitRegistry& operator=(UnitRegistry&&) = delete;

    /// Throws std::invalid_argument if `unit` is null, std::runtime_error if
    /// the registry is already at maxUnits capacity. Returns the generated
    /// id (monotonically increasing across the registry's lifetime; ids are
    /// never reused except after resetIds()).
    uint32_t add(std::unique_ptr<ProcessingUnit> unit);

    /// Same insertion as add(), but `onAdded(id, ptr)` runs while mutex_ is
    /// STILL held, before any other thread can observe or free the new
    /// unit — closes the add()-then-use-the-pointer race a bare add()
    /// leaves open (see the class comment). If `onAdded` throws, the
    /// just-added unit is rolled back (erased) before mutex_ is released
    /// and the exception propagates; the rolled-back unit's destructor
    /// still runs OUTSIDE the lock, same as remove()/clear().
    uint32_t addAndRun(std::unique_ptr<ProcessingUnit> unit,
                       const std::function<void(uint32_t, ProcessingUnit*)>& onAdded);

    /// Returns the owning unique_ptr (caller destroys it outside this call —
    /// no lock is held once this returns) or nullptr if `id` is unknown.
    std::unique_ptr<ProcessingUnit> remove(uint32_t id);

    ProcessingUnit* find(uint32_t id) const;

    /// Linear scan under mutex_ (setup-time lookup only, never RT-path).
    /// Not name-unique by construction — returns the first match.
    ProcessingUnit* findByName(const std::string& name) const;

    axonvex::optional<uint32_t> idOf(ProcessingUnit* unit) const;

    /// Snapshot vector (today's getAllProcessingUnits shape).
    std::vector<ProcessingUnit*> all() const;

    /// Calls `fn(ptr)` for every currently-registered unit while holding
    /// mutex_ for the WHOLE iteration — a concurrent clear()/remove() cannot
    /// free any of them mid-loop (see the class comment; this is
    /// startComponents()'s fix: it needs to touch every unit's pointer, not
    /// just snapshot them).
    void forEach(const std::function<void(ProcessingUnit*)>& fn) const;

    size_t count() const noexcept;

    /// Clears both tables and returns every removed unit's unique_ptr so the
    /// caller destroys them OUTSIDE this call, after mutex_ has already been
    /// released (same C18-avoidance rationale as remove(): a subclass
    /// ProcessingUnit destructor can run arbitrary user cleanup code, and
    /// bulk-destroying under the lock would be the same latent bug as
    /// erasing one unit's unique_ptr under the lock). Callers that don't
    /// need the units (emergencyShutdown/reset today don't) can discard the
    /// return value — the temporaries are destroyed after this function has
    /// already returned and released mutex_.
    ///
    /// Does NOT reset the id counter (matches today's emergencyShutdown
    /// behavior: ids keep advancing across an e-stop).
    std::vector<std::unique_ptr<ProcessingUnit>> clear();

    /// Resets the id counter to 1. Only reset() (the full lifecycle reset,
    /// as opposed to emergencyShutdown) calls this, and always together with
    /// clear() — kept as a separate method rather than a clear(bool) flag so
    /// each call site states its intent by name.
    void resetIds() noexcept;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<ProcessingUnit>> units_;
    std::unordered_map<ProcessingUnit*, uint32_t> unitToId_;
    uint32_t nextUnitId_{1};
    size_t maxUnits_;
};

} // namespace axonvex::core
