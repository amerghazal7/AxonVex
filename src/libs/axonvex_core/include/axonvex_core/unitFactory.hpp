/**
 * @file unitFactory.hpp
 * @brief Registry of ProcessingUnit types constructible by name (composer foundations).
 *
 * Design spec §3.4. Registration is expected at static-init/plugin-load/
 * startup; spec loading is the only reader path. create() validates params
 * against the registered ParamDescriptors and then runs the user-supplied
 * CreateFn OUTSIDE the registry lock — a CreateFn that re-enters the factory
 * (a composite unit creating children) must not deadlock, and no user code
 * runs under mutex_ (the C12/C18 rule, applied here pre-emptively since this
 * is new code, not a retrofit).
 */

#pragma once

#include "unitMetadata.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace axonvex::core {

class ProcessingUnit;

class UnitFactory {
  public:
    using CreateFn = std::function<std::unique_ptr<ProcessingUnit>(const std::string& instanceName,
                                                                   const nlohmann::json& params)>;

    UnitFactory() = default;
    ~UnitFactory() = default;

    // Holds a mutex: non-copyable, non-movable (C8 pattern — delete move ops,
    // never =default them). Instances remain default-constructible so tests
    // get isolation from UnitFactory::global().
    UnitFactory(const UnitFactory&) = delete;
    UnitFactory& operator=(const UnitFactory&) = delete;
    UnitFactory(UnitFactory&&) = delete;
    UnitFactory& operator=(UnitFactory&&) = delete;

    /// Refuses duplicate typeName by throwing std::logic_error (C20
    /// precedent: silent replacement would invalidate metadata other
    /// components — e.g. an already-built composer palette — captured by
    /// value or by pointer into the old registration).
    void registerType(UnitTypeDescriptor meta, CreateFn create);

    bool hasType(const std::string& typeName) const;

    /// Returns nullptr if unregistered. The returned pointer is stable for
    /// the UnitFactory's lifetime: there is no unregister/replace operation
    /// in v1, so the backing descriptor is never relocated or destroyed
    /// while the factory itself is alive.
    const UnitTypeDescriptor* describe(const std::string& typeName) const;

    std::vector<std::string> typeNames() const; ///< Palette listing.

    /// Throws std::invalid_argument (unknown type, or a param-schema
    /// violation — checked BEFORE the CreateFn runs) with a message
    /// listing every violation found (validateParams collects all of them).
    std::unique_ptr<ProcessingUnit> create(const std::string& typeName,
                                           const std::string& instanceName,
                                           const nlohmann::json& params) const;

    /// Process-wide default registry. Instances remain constructible (see
    /// above) so tests get isolation instead of sharing global state.
    static UnitFactory& global();

  private:
    struct Entry {
        UnitTypeDescriptor meta;
        CreateFn create;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> types_;
};

} // namespace axonvex::core
