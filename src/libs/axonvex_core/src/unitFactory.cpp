/**
 * @file unitFactory.cpp
 * @brief UnitFactory implementation.
 */

#include "axonvex_core/unitFactory.hpp"

#include "axonvex_core/processingUnit.hpp"

#include <stdexcept>

namespace axonvex::core {

void UnitFactory::registerType(UnitTypeDescriptor meta, CreateFn create) {
    if (meta.typeName.empty()) {
        throw std::invalid_argument("UnitFactory::registerType: typeName must not be empty");
    }
    if (!create) {
        throw std::invalid_argument(
            "UnitFactory::registerType: CreateFn must not be null for type '" + meta.typeName +
            "'");
    }

    const std::string typeName = meta.typeName;
    std::lock_guard<std::mutex> lock(mutex_);
    if (types_.find(typeName) != types_.end()) {
        // C20 precedent: silent replacement would dangle metadata/pointers
        // other components already captured from the first registration.
        throw std::logic_error(
            "UnitFactory::registerType: type '" + typeName +
            "' is already registered; duplicate registration is refused, not replaced");
    }
    types_.emplace(typeName, Entry{std::move(meta), std::move(create)});
}

bool UnitFactory::hasType(const std::string& typeName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return types_.find(typeName) != types_.end();
}

const UnitTypeDescriptor* UnitFactory::describe(const std::string& typeName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = types_.find(typeName);
    return it == types_.end() ? nullptr : &it->second.meta;
}

std::vector<std::string> UnitFactory::typeNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(types_.size());
    for (const auto& kv : types_) {
        names.push_back(kv.first);
    }
    return names;
}

std::unique_ptr<ProcessingUnit> UnitFactory::create(const std::string& typeName,
                                                    const std::string& instanceName,
                                                    const nlohmann::json& params) const {
    UnitTypeDescriptor meta;
    CreateFn createFn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = types_.find(typeName);
        if (it == types_.end()) {
            throw std::invalid_argument("UnitFactory::create: unknown unit type '" + typeName +
                                        "'");
        }
        // Copy out while locked; the CreateFn runs OUTSIDE the lock below —
        // it is user code and must never run under mutex_ (C12/C18 rule).
        meta = it->second.meta;
        createFn = it->second.create;
    }

    auto issues = validateParams(meta, params);
    if (!issues.empty()) {
        std::string message =
            "UnitFactory::create: parameter validation failed for type '" + typeName + "':";
        for (const auto& issue : issues) {
            message += " [" + issue.code + "] " + issue.message + ";";
        }
        throw std::invalid_argument(message);
    }

    return createFn(instanceName, params);
}

UnitFactory& UnitFactory::global() {
    static UnitFactory instance; // Meyer's singleton: thread-safe init (C++11+).
    return instance;
}

} // namespace axonvex::core
