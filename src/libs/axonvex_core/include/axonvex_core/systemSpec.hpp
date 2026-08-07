/**
 * @file systemSpec.hpp
 * @brief SystemSpec v1 (.axv.json) parsing and validation (composer foundations).
 *
 * Design spec §2 + §4.1/§4.2 (parse/validate slice only — this wave does NOT
 * implement SpecSystem / loadSystemFromSpec() / instantiation; see the
 * design spec for that follow-on). parse() and validate() together are a
 * standalone function pair: zero ProcessingUnit instantiation, all findable
 * errors collected in one pass (composer inline-error UX). The same pair
 * backs the future gateway spec/validate endpoint unchanged.
 */

#pragma once

#include "system.hpp" // SystemConfiguration — parse-time target only, no instantiation.
#include "timingController.hpp"
#include "unitFactory.hpp"
#include "utils/optional.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace axonvex::core {

/// Stable machine-readable error codes. Not an enum: SpecError.code travels
/// to the composer/gateway as a JSON string, so a plain string constant
/// avoids an enum<->string translation layer that would need to stay in
/// lockstep across a wire boundary.
namespace SpecErrorCode {
constexpr const char* SPEC_VERSION_UNSUPPORTED = "SPEC_VERSION_UNSUPPORTED";
constexpr const char* MISSING_FIELD = "MISSING_FIELD";
constexpr const char* INVALID_TYPE = "INVALID_TYPE";
constexpr const char* INVALID_ENUM_VALUE = "INVALID_ENUM_VALUE";
constexpr const char* UNKNOWN_KEY = "UNKNOWN_KEY";
constexpr const char* DUP_UNIT_NAME = "DUP_UNIT_NAME";
constexpr const char* UNKNOWN_UNIT_TYPE = "UNKNOWN_UNIT_TYPE";
constexpr const char* PARAM_CONSTRAINT = "PARAM_CONSTRAINT";
constexpr const char* BAD_REFERENCE = "BAD_REFERENCE";
constexpr const char* KIND_MISMATCH = "KIND_MISMATCH";
constexpr const char* PORT_TYPE_MISMATCH = "PORT_TYPE_MISMATCH";
constexpr const char* REQUIRED_INPUT_UNWIRED = "REQUIRED_INPUT_UNWIRED";
/// Reserved for the loader (design spec §4.2 step 3 / §7 Q7), which does not
/// exist yet — validate() never emits this in this wave. Defined here so the
/// wiring layer that lands next wave has a stable code to reuse instead of
/// inventing a new one.
constexpr const char* MISSING_ADAPTER = "MISSING_ADAPTER";
/// Also reserved for the loader: missionPipelines parses/validates its shape
/// (Q7) but nothing consumes it until MissionElement factories exist.
constexpr const char* UNSUPPORTED_SECTION = "UNSUPPORTED_SECTION";
} // namespace SpecErrorCode

/// path is JSON-pointer-ish (e.g. "units[2].params.window"); code is one of
/// SpecErrorCode's constants; message is human text.
struct SpecError {
    std::string path;
    std::string code;
    std::string message;
};

struct SpecUnit {
    std::string name;
    std::string type;
    nlohmann::json params = nlohmann::json::object();
    bool hasTiming{false};
    TimingConstraints timing;
    int downSamplingFactor{1};
};

struct SpecConnection {
    std::string from; ///< "unitName.portName"
    std::string to;
};

struct SpecSystemPorts {
    std::unordered_map<std::string, std::string> inputs;  ///< external name -> "unit.port"
    std::unordered_map<std::string, std::string> outputs; ///< external name -> "unit.port"
};

struct SpecAdapter {
    std::string uri;
    std::string type;
    nlohmann::json config = nlohmann::json::object();
};

struct SpecSafetyPolicy {
    std::string type;
    nlohmann::json params = nlohmann::json::object();
};

struct SpecSafety {
    int evaluationPeriodMs{100};
    std::vector<SpecSafetyPolicy> policies;
};

/// v1: reserved-but-rejected (design spec Q7). The shape below is parsed and
/// structurally validated so the composer file format is stable across the
/// wave that adds MissionElement factories (Phase 7); nothing consumes these
/// fields yet, and the future loader rejects the section with
/// SpecErrorCode::UNSUPPORTED_SECTION rather than silently ignoring it.
struct SpecMissionElement {
    std::string name;
    std::string type;
    nlohmann::json params = nlohmann::json::object();
};

struct SpecMissionTransition {
    std::string from;
    std::string to;
    std::string on; ///< "success" | "failure" | "abort"
};

struct SpecMissionPipeline {
    std::string name;
    std::vector<SpecMissionElement> elements;
    std::vector<SpecMissionTransition> transitions;
};

/// A fully parsed, structurally-valid .axv.json document. Constructing one
/// (via parse()) never instantiates a ProcessingUnit or touches a factory;
/// validate() is the factory-aware second pass.
struct SystemSpec {
    std::string specVersion;
    std::string systemName;
    std::chrono::microseconds tickRate{100};
    SchedulingPolicy schedulingPolicy{SchedulingPolicy::PRIORITY_BASED};
    bool enableRealTimeScheduling{true};
    LogLevel logLevel{LogLevel::Info};
    size_t eventPoolSize{1024};
    size_t eventQueueSize{256};
    size_t maxProcessingUnits{1000};

    std::vector<SpecUnit> units;
    std::vector<SpecConnection> connections;
    SpecSystemPorts systemPorts;
    std::vector<SpecAdapter> adapters;
    bool hasSafety{false};
    SpecSafety safety;
    std::vector<SpecMissionPipeline> missionPipelines;

    /// JSON shape, unknown-key rejection, enum decoding, unit-name
    /// uniqueness, "unit.port" reference syntax. Collects every error found
    /// (composer inline-error UX) — on ANY error, returns nullopt (design
    /// spec §4.2 step 1: "nothing constructed").
    static axonvex::optional<SystemSpec> parse(const nlohmann::json& doc,
                                               std::vector<SpecError>& errors);

    /// Full static validation against `factory`: unit types exist, params
    /// satisfy their ParamDescriptors, connection endpoints exist with
    /// matching direction/kind/dataType, required inputs are wired, system
    /// port references resolve, adapter URIs are well-formed. Zero
    /// instantiation, all errors collected. Returns true iff `errors` stays
    /// empty. This same function backs the future gateway spec/validate
    /// endpoint (design spec §4.1).
    bool validate(const UnitFactory& factory, std::vector<SpecError>& errors) const;

    /// Maps the `system` section onto the subset of SystemConfiguration the
    /// spec format covers; every other field keeps SystemConfiguration's own
    /// default. Pure data mapping — does not construct or touch a system.
    SystemConfiguration systemConfiguration() const;
};

} // namespace axonvex::core
