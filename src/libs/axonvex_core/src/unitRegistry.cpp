#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/unitRegistry.hpp>
#include <stdexcept>

namespace axonvex::core {

UnitRegistry::UnitRegistry(size_t maxUnits) : maxUnits_(maxUnits) {}

uint32_t UnitRegistry::add(std::unique_ptr<ProcessingUnit> unit) {
    if (!unit) {
        throw std::invalid_argument("Processing unit cannot be null");
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (units_.size() >= maxUnits_) {
        throw std::runtime_error("Maximum number of processing units exceeded");
    }

    uint32_t unitId = nextUnitId_++;
    ProcessingUnit* unitPtr = unit.get();
    units_[unitId] = std::move(unit);
    unitToId_[unitPtr] = unitId;
    return unitId;
}

uint32_t UnitRegistry::addAndRun(std::unique_ptr<ProcessingUnit> unit,
                                 const std::function<void(uint32_t, ProcessingUnit*)>& onAdded) {
    if (!unit) {
        throw std::invalid_argument("Processing unit cannot be null");
    }

    // Destroyed after lock.unlock() below on the rollback path, so a
    // finalize()-calling destructor never runs under mutex_ (same
    // C18-avoidance contract as remove()/clear()).
    std::unique_ptr<ProcessingUnit> rollback;
    uint32_t unitId;
    std::unique_lock<std::mutex> lock(mutex_);

    if (units_.size() >= maxUnits_) {
        throw std::runtime_error("Maximum number of processing units exceeded");
    }

    unitId = nextUnitId_++;
    ProcessingUnit* unitPtr = unit.get();
    units_[unitId] = std::move(unit);
    unitToId_[unitPtr] = unitId;

    try {
        onAdded(unitId, unitPtr);
    } catch (...) {
        auto it = units_.find(unitId);
        rollback = std::move(it->second);
        units_.erase(it);
        unitToId_.erase(unitPtr);
        lock.unlock();
        throw; // rollback destructs here, unlocked
    }

    return unitId;
}

std::unique_ptr<ProcessingUnit> UnitRegistry::remove(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = units_.find(id);
    if (it == units_.end()) {
        return nullptr;
    }

    std::unique_ptr<ProcessingUnit> owned = std::move(it->second);
    unitToId_.erase(owned.get());
    units_.erase(it);
    return owned; // destroyed by the caller, outside mutex_
}

ProcessingUnit* UnitRegistry::find(uint32_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = units_.find(id);
    return it != units_.end() ? it->second.get() : nullptr;
}

ProcessingUnit* UnitRegistry::findByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : units_) {
        if (pair.second && pair.second->getName() == name) {
            return pair.second.get();
        }
    }
    return nullptr;
}

axonvex::optional<uint32_t> UnitRegistry::idOf(ProcessingUnit* unit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = unitToId_.find(unit);
    if (it == unitToId_.end()) {
        return axonvex::nullopt;
    }
    return it->second;
}

std::vector<ProcessingUnit*> UnitRegistry::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProcessingUnit*> units;
    units.reserve(units_.size());
    for (const auto& pair : units_) {
        units.push_back(pair.second.get());
    }
    return units;
}

void UnitRegistry::forEach(const std::function<void(ProcessingUnit*)>& fn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : units_) {
        fn(pair.second.get());
    }
}

size_t UnitRegistry::count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return units_.size();
}

std::vector<std::unique_ptr<ProcessingUnit>> UnitRegistry::clear() {
    std::vector<std::unique_ptr<ProcessingUnit>> removed;
    std::lock_guard<std::mutex> lock(mutex_);
    removed.reserve(units_.size());
    for (auto& pair : units_) {
        removed.push_back(std::move(pair.second));
    }
    units_.clear();
    unitToId_.clear();
    return removed; // destroyed by the caller, outside mutex_
}

void UnitRegistry::resetIds() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    nextUnitId_ = 1;
}

} // namespace axonvex::core
