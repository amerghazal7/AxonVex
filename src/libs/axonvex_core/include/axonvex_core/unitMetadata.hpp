/**
 * @file unitMetadata.hpp
 * @brief Introspection metadata contract for ProcessingUnit types (composer foundations).
 *
 * Phase 2 design spec §3: this is the currency the UnitFactory registers types
 * with, the SystemSpec validator checks specs against, and (later) the
 * gateway spec/* API and v1.1 Visual System Composer palette serialize.
 *
 * nlohmann::json is the parameter/metadata currency by decision (design spec
 * Q2): it is already core-public via configuration.hpp and it's what the
 * .axv.json spec parses into, so there is no conversion layer and no new
 * owned ParamMap type.
 */

#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace axonvex::core {

/// Canonical, portable port data-type identity (design spec §3.2).
///
/// BasePort::getDataTypeName() returns typeid(T).name() today — mangled,
/// compiler-specific, useless for a document format. This trait is the
/// replacement identity used by descriptors/spec validation; it does NOT
/// touch BasePort (that migration is Track PORTS' territory, a later wave).
///
/// The primary template is intentionally left undefined: using an
/// unregistered T fails to compile (PortDataTypeName<T>::value() on an
/// incomplete type), not silently at runtime.
template <typename T>
struct PortDataTypeName;

} // namespace axonvex::core

/// Registers T's canonical dot-namespaced name. Core pre-registers the five
/// primitives below; plugins register their own project structs in their
/// headers as they are exposed to specs (design spec §3.2). Declared outside
/// any namespace (as an explicit specialization of an axonvex::core template)
/// so callers can invoke it from wherever their type is declared.
#define AXONVEX_REGISTER_PORT_DATA_TYPE(T, NAME)                                                   \
    template <>                                                                                    \
    struct axonvex::core::PortDataTypeName<T> {                                                    \
        static const char* value() {                                                               \
            return NAME;                                                                           \
        }                                                                                          \
    };

namespace axonvex::core {

/// Port direction as seen from the unit's own boundary (an Output port is
/// something the unit writes to; an Input port is something it reads from).
struct PortDescriptor {
    enum class Direction { Input, Output };
    enum class Kind { Sync, Async };

    int index{0}; ///< Port id inside the unit (ProcessingUnit's int index namespace).
    std::string name;
    Direction direction{Direction::Input};
    Kind kind{Kind::Sync};
    std::string dataType; ///< Canonical name (see PortDataTypeName<T>).
    bool required{true};  ///< Validator: required inputs must be wired (outputs ignore this).

    /// Convenience constructor: derives dataType from T via PortDataTypeName<T>.
    /// Fails to compile for an unregistered T (see PortDataTypeName above).
    template <typename T>
    static PortDescriptor make(int idx, std::string portName, Direction dir, Kind portKind,
                               bool isRequired = true) {
        PortDescriptor d;
        d.index = idx;
        d.name = std::move(portName);
        d.direction = dir;
        d.kind = portKind;
        d.dataType = PortDataTypeName<T>::value();
        d.required = isRequired;
        return d;
    }

    nlohmann::json toJson() const;
};

/// One configurable parameter of a unit type. Parsed spec params are matched
/// against these by name; unmatched spec keys are rejected (typo protection),
/// and a null defaultValue marks the parameter required.
struct ParamDescriptor {
    enum class Type { Bool, Int, Double, String, Enum, Object };

    std::string name;
    Type type{Type::String};
    nlohmann::json defaultValue; ///< null == required parameter.
    std::string description;
    nlohmann::json constraints; ///< e.g. {"min":1,"max":64} or {"values":[...]} for Enum.

    // Explicit constructor, not defaulted: a default member initializer
    // above (Type type{Type::String}) disqualifies this struct as an
    // aggregate pre-C++17, so `ParamDescriptor{"x", Type::Int, ...}` call
    // sites (builtinUnits.cpp, tests) need a real constructor to bind to
    // under C++14 — aggregate-init syntax on a non-aggregate is not legal
    // until P0017 (C++17).
    ParamDescriptor() = default;
    ParamDescriptor(std::string paramName, Type paramType, nlohmann::json defaultVal,
                    std::string desc, nlohmann::json paramConstraints)
        : name(std::move(paramName)), type(paramType), defaultValue(std::move(defaultVal)),
          description(std::move(desc)), constraints(std::move(paramConstraints)) {}

    bool isRequired() const noexcept {
        return defaultValue.is_null();
    }

    nlohmann::json toJson() const;
};

/// Full introspection record for one unit type: what the factory registers,
/// what the validator checks specs against, what the composer palette shows.
struct UnitTypeDescriptor {
    std::string typeName; ///< Dot-namespaced, e.g. "axonvex.MovingAverage". Plugin prefix
                          ///< mandatory for plugin-registered types.
    std::string description;
    std::vector<PortDescriptor> ports;
    std::vector<ParamDescriptor> parameters;

    /// Powers the gateway spec API + composer palette.
    nlohmann::json toJson() const;
};

/// One parameter-validation finding, produced by validateParams() below.
/// `code` is one of "UNKNOWN_KEY", "MISSING_REQUIRED", "TYPE_MISMATCH",
/// "CONSTRAINT_VIOLATION" — callers (UnitFactory::create, SystemSpec::validate)
/// translate these into their own error shapes (an exception message, a
/// SpecError with a JSON-pointer-ish path) rather than this shared helper
/// owning either presentation.
struct ParamValidationIssue {
    std::string paramName; ///< The offending/missing key name.
    std::string code;
    std::string message;
};

/// Checks `params` (a JSON object, spec-provided or user-provided) against
/// `meta`'s ParamDescriptors: unknown keys, missing required params, JSON
/// type mismatches, and (best-effort) min/max/values constraints. Shared by
/// UnitFactory::create() (pre-flight, before the user CreateFn runs) and
/// SystemSpec::validate() (composer inline-error UX) so the two never drift.
std::vector<ParamValidationIssue> validateParams(const UnitTypeDescriptor& meta,
                                                 const nlohmann::json& params);

} // namespace axonvex::core

// Core-registered primitive canonical names (design spec §3.2). Cross-plugin
// identity is by string convention with registration-time collision refusal
// in UnitFactory — no sizeof/layout hash in v1 (open question Q3); if
// cross-plugin type confusion ever bites in practice, that hash is the
// upgrade path.
AXONVEX_REGISTER_PORT_DATA_TYPE(bool, "bool")
AXONVEX_REGISTER_PORT_DATA_TYPE(std::int32_t, "int32")
AXONVEX_REGISTER_PORT_DATA_TYPE(std::int64_t, "int64")
AXONVEX_REGISTER_PORT_DATA_TYPE(double, "double")
AXONVEX_REGISTER_PORT_DATA_TYPE(std::string, "string")
