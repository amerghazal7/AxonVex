/**
 * @file systemSpec.cpp
 * @brief SystemSpec v1 (.axv.json) parsing and validation implementation.
 */

#include "axonvex_core/systemSpec.hpp"

#include <algorithm>
#include <unordered_set>

namespace axonvex::core {

namespace {

using Code = const char*;

void addError(std::vector<SpecError>& errors, std::string path, Code code, std::string message) {
    errors.push_back(SpecError{std::move(path), code, std::move(message)});
}

void checkUnknownKeys(const nlohmann::json& obj, const std::vector<std::string>& allowed,
                      const std::string& path, std::vector<SpecError>& errors) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end()) {
            addError(errors, path + "." + it.key(), SpecErrorCode::UNKNOWN_KEY,
                     "unknown key '" + it.key() + "'");
        }
    }
}

axonvex::optional<SchedulingPolicy> parseSchedulingPolicy(const std::string& s) {
    if (s == "ROUND_ROBIN")
        return SchedulingPolicy::ROUND_ROBIN;
    if (s == "PRIORITY_BASED")
        return SchedulingPolicy::PRIORITY_BASED;
    if (s == "EARLIEST_DEADLINE_FIRST")
        return SchedulingPolicy::EARLIEST_DEADLINE_FIRST;
    if (s == "RATE_MONOTONIC")
        return SchedulingPolicy::RATE_MONOTONIC;
    // CUSTOM intentionally excluded: design spec §7 Q6 flags it for deletion
    // (it cannot honor the RT-path invariant); the spec format never accepted
    // it, so this is not a regression relative to some prior wider grammar.
    return axonvex::nullopt;
}

axonvex::optional<LogLevel> parseLogLevel(const std::string& s) {
    if (s == "Debug")
        return LogLevel::Debug;
    if (s == "Info")
        return LogLevel::Info;
    if (s == "Warning")
        return LogLevel::Warning;
    if (s == "Error")
        return LogLevel::Error;
    if (s == "Critical")
        return LogLevel::Critical;
    return axonvex::nullopt;
}

axonvex::optional<SchedulerPriority> parseSchedulerPriority(const std::string& s) {
    if (s == "IDLE")
        return SchedulerPriority::IDLE;
    if (s == "LOW")
        return SchedulerPriority::LOW;
    if (s == "NORMAL")
        return SchedulerPriority::NORMAL;
    if (s == "HIGH")
        return SchedulerPriority::HIGH;
    if (s == "REAL_TIME")
        return SchedulerPriority::REAL_TIME;
    if (s == "CRITICAL")
        return SchedulerPriority::CRITICAL;
    return axonvex::nullopt;
}

const char* kindName(PortDescriptor::Kind k) {
    return k == PortDescriptor::Kind::Sync ? "Sync" : "Async";
}

/// "unit.port" syntax: exactly one '.', non-empty on both sides.
bool isValidPortRef(const std::string& s) {
    auto pos = s.find('.');
    if (pos == std::string::npos || pos == 0 || pos == s.size() - 1) {
        return false;
    }
    return s.find('.', pos + 1) == std::string::npos;
}

void splitPortRef(const std::string& ref, std::string& unitName, std::string& portName) {
    auto pos = ref.find('.');
    unitName = ref.substr(0, pos);
    portName = ref.substr(pos + 1);
}

// ---------------------------------------------------------------- section parsers

void parseSystemSection(const nlohmann::json& doc, SystemSpec& spec,
                        std::vector<SpecError>& errors) {
    if (!doc.contains("system")) {
        addError(errors, "$.system", SpecErrorCode::MISSING_FIELD, "system section is required");
        return;
    }
    const nlohmann::json& sys = doc.at("system");
    if (!sys.is_object()) {
        addError(errors, "$.system", SpecErrorCode::INVALID_TYPE, "system must be an object");
        return;
    }
    static const std::vector<std::string> kAllowed = {
        "name",     "tickRateUs",    "schedulingPolicy", "enableRealTimeScheduling",
        "logLevel", "eventPoolSize", "eventQueueSize",   "maxProcessingUnits"};
    checkUnknownKeys(sys, kAllowed, "$.system", errors);

    if (!sys.contains("name")) {
        addError(errors, "$.system.name", SpecErrorCode::MISSING_FIELD, "system.name is required");
    } else if (!sys.at("name").is_string() || sys.at("name").get<std::string>().empty()) {
        addError(errors, "$.system.name", SpecErrorCode::INVALID_TYPE,
                 "system.name must be a non-empty string");
    } else {
        spec.systemName = sys.at("name").get<std::string>();
    }

    if (sys.contains("tickRateUs")) {
        if (!sys.at("tickRateUs").is_number_integer() || sys.at("tickRateUs").get<int64_t>() <= 0) {
            addError(errors, "$.system.tickRateUs", SpecErrorCode::PARAM_CONSTRAINT,
                     "tickRateUs must be a positive integer");
        } else {
            spec.tickRate = std::chrono::microseconds(sys.at("tickRateUs").get<int64_t>());
        }
    }

    if (sys.contains("schedulingPolicy")) {
        if (!sys.at("schedulingPolicy").is_string()) {
            addError(errors, "$.system.schedulingPolicy", SpecErrorCode::INVALID_TYPE,
                     "schedulingPolicy must be a string");
        } else {
            auto parsed = parseSchedulingPolicy(sys.at("schedulingPolicy").get<std::string>());
            if (!parsed) {
                addError(errors, "$.system.schedulingPolicy", SpecErrorCode::INVALID_ENUM_VALUE,
                         "unknown schedulingPolicy '" +
                             sys.at("schedulingPolicy").get<std::string>() + "'");
            } else {
                spec.schedulingPolicy = *parsed;
            }
        }
    }

    if (sys.contains("enableRealTimeScheduling")) {
        if (!sys.at("enableRealTimeScheduling").is_boolean()) {
            addError(errors, "$.system.enableRealTimeScheduling", SpecErrorCode::INVALID_TYPE,
                     "enableRealTimeScheduling must be a boolean");
        } else {
            spec.enableRealTimeScheduling = sys.at("enableRealTimeScheduling").get<bool>();
        }
    }

    if (sys.contains("logLevel")) {
        if (!sys.at("logLevel").is_string()) {
            addError(errors, "$.system.logLevel", SpecErrorCode::INVALID_TYPE,
                     "logLevel must be a string");
        } else {
            auto parsed = parseLogLevel(sys.at("logLevel").get<std::string>());
            if (!parsed) {
                addError(errors, "$.system.logLevel", SpecErrorCode::INVALID_ENUM_VALUE,
                         "unknown logLevel '" + sys.at("logLevel").get<std::string>() + "'");
            } else {
                spec.logLevel = *parsed;
            }
        }
    }

    auto parsePositiveSizeField = [&](const char* key, size_t& target) {
        if (!sys.contains(key)) {
            return;
        }
        if (!sys.at(key).is_number_integer() || sys.at(key).get<int64_t>() <= 0) {
            addError(errors, std::string("$.system.") + key, SpecErrorCode::PARAM_CONSTRAINT,
                     std::string(key) + " must be a positive integer");
        } else {
            target = static_cast<size_t>(sys.at(key).get<int64_t>());
        }
    };
    parsePositiveSizeField("eventPoolSize", spec.eventPoolSize);
    parsePositiveSizeField("eventQueueSize", spec.eventQueueSize);
    parsePositiveSizeField("maxProcessingUnits", spec.maxProcessingUnits);
}

void parseUnitsSection(const nlohmann::json& doc, SystemSpec& spec,
                       std::vector<SpecError>& errors) {
    if (!doc.contains("units")) {
        addError(errors, "$.units", SpecErrorCode::MISSING_FIELD, "units is required");
        return;
    }
    const nlohmann::json& units = doc.at("units");
    if (!units.is_array() || units.empty()) {
        addError(errors, "$.units", SpecErrorCode::PARAM_CONSTRAINT,
                 "units must be a non-empty array");
        return;
    }
    static const std::vector<std::string> kAllowed = {"name", "type", "params", "timing",
                                                      "downSamplingFactor"};
    static const std::vector<std::string> kTimingAllowed = {"periodUs", "deadlineUs", "wcetUs",
                                                            "priority", "isRealTime"};
    std::unordered_map<std::string, size_t> seenNames;

    for (size_t i = 0; i < units.size(); ++i) {
        std::string base = "$.units[" + std::to_string(i) + "]";
        const nlohmann::json& u = units[i];
        if (!u.is_object()) {
            addError(errors, base, SpecErrorCode::INVALID_TYPE, "unit entry must be an object");
            continue;
        }
        checkUnknownKeys(u, kAllowed, base, errors);

        SpecUnit su;
        if (!u.contains("name")) {
            addError(errors, base + ".name", SpecErrorCode::MISSING_FIELD, "unit name is required");
        } else if (!u.at("name").is_string() || u.at("name").get<std::string>().empty()) {
            addError(errors, base + ".name", SpecErrorCode::INVALID_TYPE,
                     "unit name must be a non-empty string");
        } else {
            su.name = u.at("name").get<std::string>();
            auto seen = seenNames.find(su.name);
            if (seen != seenNames.end()) {
                addError(errors, base + ".name", SpecErrorCode::DUP_UNIT_NAME,
                         "duplicate unit name '" + su.name + "' (first seen at units[" +
                             std::to_string(seen->second) + "])");
            } else {
                seenNames[su.name] = i;
            }
        }

        if (!u.contains("type")) {
            addError(errors, base + ".type", SpecErrorCode::MISSING_FIELD, "unit type is required");
        } else if (!u.at("type").is_string() || u.at("type").get<std::string>().empty()) {
            addError(errors, base + ".type", SpecErrorCode::INVALID_TYPE,
                     "unit type must be a non-empty string");
        } else {
            su.type = u.at("type").get<std::string>();
        }

        if (u.contains("params")) {
            if (!u.at("params").is_object()) {
                addError(errors, base + ".params", SpecErrorCode::INVALID_TYPE,
                         "params must be an object");
            } else {
                su.params = u.at("params");
            }
        }

        if (u.contains("timing")) {
            const nlohmann::json& t = u.at("timing");
            if (!t.is_object()) {
                addError(errors, base + ".timing", SpecErrorCode::INVALID_TYPE,
                         "timing must be an object");
            } else {
                checkUnknownKeys(t, kTimingAllowed, base + ".timing", errors);
                su.hasTiming = true;

                auto parseDurationField = [&](const char* key, std::chrono::microseconds& target) {
                    if (!t.contains(key)) {
                        return;
                    }
                    if (!t.at(key).is_number_integer() || t.at(key).get<int64_t>() < 0) {
                        addError(errors, base + ".timing." + key, SpecErrorCode::PARAM_CONSTRAINT,
                                 std::string(key) + " must be a non-negative integer");
                    } else {
                        target = std::chrono::microseconds(t.at(key).get<int64_t>());
                    }
                };
                parseDurationField("periodUs", su.timing.period);
                parseDurationField("deadlineUs", su.timing.deadline);
                parseDurationField("wcetUs", su.timing.wcet);

                if (t.contains("priority")) {
                    if (!t.at("priority").is_string()) {
                        addError(errors, base + ".timing.priority", SpecErrorCode::INVALID_TYPE,
                                 "priority must be a string");
                    } else {
                        auto parsed = parseSchedulerPriority(t.at("priority").get<std::string>());
                        if (!parsed) {
                            addError(
                                errors, base + ".timing.priority",
                                SpecErrorCode::INVALID_ENUM_VALUE,
                                "unknown priority '" + t.at("priority").get<std::string>() + "'");
                        } else {
                            su.timing.priority = *parsed;
                        }
                    }
                }
                if (t.contains("isRealTime")) {
                    if (!t.at("isRealTime").is_boolean()) {
                        addError(errors, base + ".timing.isRealTime", SpecErrorCode::INVALID_TYPE,
                                 "isRealTime must be a boolean");
                    } else {
                        su.timing.isRealTime = t.at("isRealTime").get<bool>();
                    }
                }
            }
        }

        if (u.contains("downSamplingFactor")) {
            if (!u.at("downSamplingFactor").is_number_integer() ||
                u.at("downSamplingFactor").get<int64_t>() < 1) {
                addError(errors, base + ".downSamplingFactor", SpecErrorCode::PARAM_CONSTRAINT,
                         "downSamplingFactor must be an integer >= 1");
            } else {
                su.downSamplingFactor = static_cast<int>(u.at("downSamplingFactor").get<int64_t>());
            }
        }

        spec.units.push_back(std::move(su));
    }
}

void parseConnectionsSection(const nlohmann::json& doc, SystemSpec& spec,
                             std::vector<SpecError>& errors) {
    if (!doc.contains("connections")) {
        return; // optional
    }
    const nlohmann::json& conns = doc.at("connections");
    if (!conns.is_array()) {
        addError(errors, "$.connections", SpecErrorCode::INVALID_TYPE,
                 "connections must be an array");
        return;
    }
    static const std::vector<std::string> kAllowed = {"from", "to"};

    for (size_t i = 0; i < conns.size(); ++i) {
        std::string base = "$.connections[" + std::to_string(i) + "]";
        const nlohmann::json& c = conns[i];
        if (!c.is_object()) {
            addError(errors, base, SpecErrorCode::INVALID_TYPE,
                     "connection entry must be an object");
            continue;
        }
        checkUnknownKeys(c, kAllowed, base, errors);

        SpecConnection sc;
        bool ok = true;
        auto checkRef = [&](const char* key, std::string& target) {
            if (!c.contains(key)) {
                addError(errors, base + "." + key, SpecErrorCode::MISSING_FIELD,
                         std::string(key) + " is required");
                ok = false;
            } else if (!c.at(key).is_string()) {
                addError(errors, base + "." + key, SpecErrorCode::INVALID_TYPE,
                         std::string(key) + " must be a string");
                ok = false;
            } else if (!isValidPortRef(c.at(key).get<std::string>())) {
                addError(errors, base + "." + key, SpecErrorCode::BAD_REFERENCE,
                         std::string(key) + " must be a 'unit.port' reference");
                ok = false;
            } else {
                target = c.at(key).get<std::string>();
            }
        };
        checkRef("from", sc.from);
        checkRef("to", sc.to);
        if (ok) {
            spec.connections.push_back(std::move(sc));
        }
    }
}

void parseSystemPortsSection(const nlohmann::json& doc, SystemSpec& spec,
                             std::vector<SpecError>& errors) {
    if (!doc.contains("systemPorts")) {
        return;
    }
    const nlohmann::json& sp = doc.at("systemPorts");
    if (!sp.is_object()) {
        addError(errors, "$.systemPorts", SpecErrorCode::INVALID_TYPE,
                 "systemPorts must be an object");
        return;
    }
    static const std::vector<std::string> kAllowed = {"inputs", "outputs"};
    checkUnknownKeys(sp, kAllowed, "$.systemPorts", errors);

    auto parseMap = [&](const char* key, std::unordered_map<std::string, std::string>& target) {
        if (!sp.contains(key)) {
            return;
        }
        const nlohmann::json& m = sp.at(key);
        if (!m.is_object()) {
            addError(errors, std::string("$.systemPorts.") + key, SpecErrorCode::INVALID_TYPE,
                     std::string(key) + " must be an object");
            return;
        }
        for (auto it = m.begin(); it != m.end(); ++it) {
            std::string base = std::string("$.systemPorts.") + key + "." + it.key();
            if (!it.value().is_string()) {
                addError(errors, base, SpecErrorCode::INVALID_TYPE, "must map to a string");
            } else if (!isValidPortRef(it.value().get<std::string>())) {
                addError(errors, base, SpecErrorCode::BAD_REFERENCE,
                         "must map to a 'unit.port' reference");
            } else {
                target[it.key()] = it.value().get<std::string>();
            }
        }
    };
    parseMap("inputs", spec.systemPorts.inputs);
    parseMap("outputs", spec.systemPorts.outputs);
}

void parseAdaptersSection(const nlohmann::json& doc, SystemSpec& spec,
                          std::vector<SpecError>& errors) {
    if (!doc.contains("adapters")) {
        return;
    }
    const nlohmann::json& arr = doc.at("adapters");
    if (!arr.is_array()) {
        addError(errors, "$.adapters", SpecErrorCode::INVALID_TYPE, "adapters must be an array");
        return;
    }
    static const std::vector<std::string> kAllowed = {"uri", "type", "config"};

    for (size_t i = 0; i < arr.size(); ++i) {
        std::string base = "$.adapters[" + std::to_string(i) + "]";
        const nlohmann::json& a = arr[i];
        if (!a.is_object()) {
            addError(errors, base, SpecErrorCode::INVALID_TYPE, "adapter entry must be an object");
            continue;
        }
        checkUnknownKeys(a, kAllowed, base, errors);

        SpecAdapter sa;
        if (!a.contains("uri") || !a.at("uri").is_string() ||
            a.at("uri").get<std::string>().empty()) {
            addError(errors, base + ".uri",
                     a.contains("uri") ? SpecErrorCode::INVALID_TYPE : SpecErrorCode::MISSING_FIELD,
                     "uri is required and must be a non-empty string");
        } else {
            sa.uri = a.at("uri").get<std::string>();
        }
        if (!a.contains("type") || !a.at("type").is_string() ||
            a.at("type").get<std::string>().empty()) {
            addError(
                errors, base + ".type",
                a.contains("type") ? SpecErrorCode::INVALID_TYPE : SpecErrorCode::MISSING_FIELD,
                "type is required and must be a non-empty string");
        } else {
            sa.type = a.at("type").get<std::string>();
        }
        if (a.contains("config")) {
            if (!a.at("config").is_object()) {
                addError(errors, base + ".config", SpecErrorCode::INVALID_TYPE,
                         "config must be an object");
            } else {
                sa.config = a.at("config");
            }
        }
        spec.adapters.push_back(std::move(sa));
    }
}

void parseSafetySection(const nlohmann::json& doc, SystemSpec& spec,
                        std::vector<SpecError>& errors) {
    if (!doc.contains("safety")) {
        return;
    }
    const nlohmann::json& s = doc.at("safety");
    if (!s.is_object()) {
        addError(errors, "$.safety", SpecErrorCode::INVALID_TYPE, "safety must be an object");
        return;
    }
    static const std::vector<std::string> kAllowed = {"evaluationPeriodMs", "policies"};
    checkUnknownKeys(s, kAllowed, "$.safety", errors);
    spec.hasSafety = true;

    if (s.contains("evaluationPeriodMs")) {
        if (!s.at("evaluationPeriodMs").is_number_integer() ||
            s.at("evaluationPeriodMs").get<int64_t>() <= 0) {
            addError(errors, "$.safety.evaluationPeriodMs", SpecErrorCode::PARAM_CONSTRAINT,
                     "evaluationPeriodMs must be a positive integer");
        } else {
            spec.safety.evaluationPeriodMs =
                static_cast<int>(s.at("evaluationPeriodMs").get<int64_t>());
        }
    }

    if (s.contains("policies")) {
        const nlohmann::json& pols = s.at("policies");
        if (!pols.is_array()) {
            addError(errors, "$.safety.policies", SpecErrorCode::INVALID_TYPE,
                     "policies must be an array");
        } else {
            static const std::vector<std::string> kPolAllowed = {"type", "params"};
            for (size_t i = 0; i < pols.size(); ++i) {
                std::string base = "$.safety.policies[" + std::to_string(i) + "]";
                const nlohmann::json& p = pols[i];
                if (!p.is_object()) {
                    addError(errors, base, SpecErrorCode::INVALID_TYPE,
                             "policy entry must be an object");
                    continue;
                }
                checkUnknownKeys(p, kPolAllowed, base, errors);
                SpecSafetyPolicy policy;
                if (!p.contains("type") || !p.at("type").is_string() ||
                    p.at("type").get<std::string>().empty()) {
                    addError(errors, base + ".type",
                             p.contains("type") ? SpecErrorCode::INVALID_TYPE
                                                : SpecErrorCode::MISSING_FIELD,
                             "policy type is required and must be a non-empty string");
                } else {
                    policy.type = p.at("type").get<std::string>();
                }
                if (p.contains("params")) {
                    if (!p.at("params").is_object()) {
                        addError(errors, base + ".params", SpecErrorCode::INVALID_TYPE,
                                 "params must be an object");
                    } else {
                        policy.params = p.at("params");
                    }
                }
                spec.safety.policies.push_back(std::move(policy));
            }
        }
    }
}

/// v1: shape-checked only (design spec §7 Q7 — reserved-but-rejected until
/// MissionElement factories exist). No factory/semantic validation here.
void parseMissionPipelinesSection(const nlohmann::json& doc, SystemSpec& spec,
                                  std::vector<SpecError>& errors) {
    if (!doc.contains("missionPipelines")) {
        return;
    }
    const nlohmann::json& arr = doc.at("missionPipelines");
    if (!arr.is_array()) {
        addError(errors, "$.missionPipelines", SpecErrorCode::INVALID_TYPE,
                 "missionPipelines must be an array");
        return;
    }
    static const std::vector<std::string> kAllowed = {"name", "elements", "transitions"};
    static const std::vector<std::string> kElemAllowed = {"name", "type", "params"};
    static const std::vector<std::string> kTransAllowed = {"from", "to", "on"};

    for (size_t i = 0; i < arr.size(); ++i) {
        std::string base = "$.missionPipelines[" + std::to_string(i) + "]";
        const nlohmann::json& mp = arr[i];
        if (!mp.is_object()) {
            addError(errors, base, SpecErrorCode::INVALID_TYPE,
                     "missionPipeline entry must be an object");
            continue;
        }
        checkUnknownKeys(mp, kAllowed, base, errors);

        SpecMissionPipeline pipeline;
        if (!mp.contains("name") || !mp.at("name").is_string() ||
            mp.at("name").get<std::string>().empty()) {
            addError(
                errors, base + ".name",
                mp.contains("name") ? SpecErrorCode::INVALID_TYPE : SpecErrorCode::MISSING_FIELD,
                "missionPipeline name is required and must be a non-empty string");
        } else {
            pipeline.name = mp.at("name").get<std::string>();
        }

        if (mp.contains("elements")) {
            const nlohmann::json& els = mp.at("elements");
            if (!els.is_array()) {
                addError(errors, base + ".elements", SpecErrorCode::INVALID_TYPE,
                         "elements must be an array");
            } else {
                for (size_t j = 0; j < els.size(); ++j) {
                    std::string ebase = base + ".elements[" + std::to_string(j) + "]";
                    const nlohmann::json& e = els[j];
                    if (!e.is_object()) {
                        addError(errors, ebase, SpecErrorCode::INVALID_TYPE,
                                 "element entry must be an object");
                        continue;
                    }
                    checkUnknownKeys(e, kElemAllowed, ebase, errors);
                    SpecMissionElement elem;
                    if (!e.contains("name") || !e.at("name").is_string()) {
                        addError(errors, ebase + ".name", SpecErrorCode::MISSING_FIELD,
                                 "element name is required");
                    } else {
                        elem.name = e.at("name").get<std::string>();
                    }
                    if (!e.contains("type") || !e.at("type").is_string()) {
                        addError(errors, ebase + ".type", SpecErrorCode::MISSING_FIELD,
                                 "element type is required");
                    } else {
                        elem.type = e.at("type").get<std::string>();
                    }
                    if (e.contains("params")) {
                        if (!e.at("params").is_object()) {
                            addError(errors, ebase + ".params", SpecErrorCode::INVALID_TYPE,
                                     "params must be an object");
                        } else {
                            elem.params = e.at("params");
                        }
                    }
                    pipeline.elements.push_back(std::move(elem));
                }
            }
        }

        if (mp.contains("transitions")) {
            const nlohmann::json& trs = mp.at("transitions");
            if (!trs.is_array()) {
                addError(errors, base + ".transitions", SpecErrorCode::INVALID_TYPE,
                         "transitions must be an array");
            } else {
                for (size_t j = 0; j < trs.size(); ++j) {
                    std::string tbase = base + ".transitions[" + std::to_string(j) + "]";
                    const nlohmann::json& t = trs[j];
                    if (!t.is_object()) {
                        addError(errors, tbase, SpecErrorCode::INVALID_TYPE,
                                 "transition entry must be an object");
                        continue;
                    }
                    checkUnknownKeys(t, kTransAllowed, tbase, errors);
                    SpecMissionTransition trans;
                    if (!t.contains("from") || !t.at("from").is_string()) {
                        addError(errors, tbase + ".from", SpecErrorCode::MISSING_FIELD,
                                 "transition 'from' is required");
                    } else {
                        trans.from = t.at("from").get<std::string>();
                    }
                    if (!t.contains("to") || !t.at("to").is_string()) {
                        addError(errors, tbase + ".to", SpecErrorCode::MISSING_FIELD,
                                 "transition 'to' is required");
                    } else {
                        trans.to = t.at("to").get<std::string>();
                    }
                    if (!t.contains("on") || !t.at("on").is_string()) {
                        addError(errors, tbase + ".on", SpecErrorCode::MISSING_FIELD,
                                 "transition 'on' is required");
                    } else {
                        std::string onVal = t.at("on").get<std::string>();
                        if (onVal != "success" && onVal != "failure" && onVal != "abort") {
                            addError(errors, tbase + ".on", SpecErrorCode::INVALID_ENUM_VALUE,
                                     "'on' must be one of success|failure|abort");
                        } else {
                            trans.on = onVal;
                        }
                    }
                    pipeline.transitions.push_back(std::move(trans));
                }
            }
        }

        spec.missionPipelines.push_back(std::move(pipeline));
    }
}

} // namespace

axonvex::optional<SystemSpec> SystemSpec::parse(const nlohmann::json& doc,
                                                std::vector<SpecError>& errors) {
    std::vector<SpecError> local;
    SystemSpec spec;

    if (!doc.is_object()) {
        addError(local, "$", SpecErrorCode::INVALID_TYPE, "document root must be a JSON object");
        errors.insert(errors.end(), local.begin(), local.end());
        return axonvex::nullopt;
    }

    static const std::vector<std::string> kTopLevelKeys = {
        "specVersion", "system",   "units",  "connections",
        "systemPorts", "adapters", "safety", "missionPipelines"};
    checkUnknownKeys(doc, kTopLevelKeys, "$", local);

    if (!doc.contains("specVersion")) {
        addError(local, "$.specVersion", SpecErrorCode::MISSING_FIELD, "specVersion is required");
    } else if (!doc.at("specVersion").is_string()) {
        addError(local, "$.specVersion", SpecErrorCode::INVALID_TYPE,
                 "specVersion must be a string");
    } else {
        spec.specVersion = doc.at("specVersion").get<std::string>();
        if (spec.specVersion != "1.0") {
            addError(
                local, "$.specVersion", SpecErrorCode::SPEC_VERSION_UNSUPPORTED,
                "unsupported specVersion '" + spec.specVersion + "'; this build understands '1.0'");
        }
    }

    parseSystemSection(doc, spec, local);
    parseUnitsSection(doc, spec, local);
    parseConnectionsSection(doc, spec, local);
    parseSystemPortsSection(doc, spec, local);
    parseAdaptersSection(doc, spec, local);
    parseSafetySection(doc, spec, local);
    parseMissionPipelinesSection(doc, spec, local);

    errors.insert(errors.end(), local.begin(), local.end());
    if (!local.empty()) {
        return axonvex::nullopt;
    }
    return spec;
}

bool SystemSpec::validate(const UnitFactory& factory, std::vector<SpecError>& errors) const {
    std::vector<SpecError> local;

    struct UnitInfo {
        std::string type;
        const UnitTypeDescriptor* descriptor;
    };
    std::unordered_map<std::string, UnitInfo> unitsByName;

    for (size_t i = 0; i < units.size(); ++i) {
        const SpecUnit& u = units[i];
        std::string base = "$.units[" + std::to_string(i) + "]";
        const UnitTypeDescriptor* descriptor = factory.describe(u.type);
        if (!descriptor) {
            addError(local, base + ".type", SpecErrorCode::UNKNOWN_UNIT_TYPE,
                     "unknown unit type '" + u.type + "'");
        } else {
            auto issues = validateParams(*descriptor, u.params);
            for (const auto& issue : issues) {
                Code code = issue.code == "UNKNOWN_KEY" ? SpecErrorCode::UNKNOWN_KEY
                                                        : SpecErrorCode::PARAM_CONSTRAINT;
                addError(local, base + ".params." + issue.paramName, code, issue.message);
            }
        }
        unitsByName[u.name] = UnitInfo{u.type, descriptor};
    }

    // Resolves "unit.port" against unitsByName's descriptors. Returns
    // nullptr (and records the appropriate error) if unresolvable; does not
    // re-report a unit whose TYPE is already unknown (that error stands on
    // its own — this one would just be noise about the same root cause).
    auto resolvePort = [&](const std::string& ref,
                           const std::string& path) -> const PortDescriptor* {
        std::string unitName, portName;
        splitPortRef(ref, unitName, portName);
        auto uit = unitsByName.find(unitName);
        if (uit == unitsByName.end()) {
            addError(local, path, SpecErrorCode::BAD_REFERENCE,
                     "no unit named '" + unitName + "' in this document");
            return nullptr;
        }
        if (!uit->second.descriptor) {
            return nullptr; // unit's type is unknown; already reported once.
        }
        for (const auto& pd : uit->second.descriptor->ports) {
            if (pd.name == portName) {
                return &pd;
            }
        }
        addError(local, path, SpecErrorCode::BAD_REFERENCE,
                 "unit '" + unitName + "' (type '" + uit->second.type + "') has no port named '" +
                     portName + "'");
        return nullptr;
    };

    std::unordered_set<std::string>
        wiredInputs; // "unit.port" wired by a connection or systemPorts.inputs

    for (size_t i = 0; i < connections.size(); ++i) {
        std::string base = "$.connections[" + std::to_string(i) + "]";
        const PortDescriptor* fromPort = resolvePort(connections[i].from, base + ".from");
        const PortDescriptor* toPort = resolvePort(connections[i].to, base + ".to");

        if (fromPort && fromPort->direction != PortDescriptor::Direction::Output) {
            addError(local, base + ".from", SpecErrorCode::BAD_REFERENCE,
                     "'" + connections[i].from + "' is not an output port");
            fromPort = nullptr;
        }
        if (toPort && toPort->direction != PortDescriptor::Direction::Input) {
            addError(local, base + ".to", SpecErrorCode::BAD_REFERENCE,
                     "'" + connections[i].to + "' is not an input port");
            toPort = nullptr;
        }
        if (fromPort && toPort) {
            if (fromPort->kind != toPort->kind) {
                addError(local, base, SpecErrorCode::KIND_MISMATCH,
                         "'" + connections[i].from + "' (" + kindName(fromPort->kind) +
                             ") cannot connect to '" + connections[i].to + "' (" +
                             kindName(toPort->kind) + ")");
            } else if (fromPort->dataType != toPort->dataType) {
                addError(local, base, SpecErrorCode::PORT_TYPE_MISMATCH,
                         "'" + connections[i].from + "' (" + fromPort->dataType +
                             ") cannot connect to '" + connections[i].to + "' (" +
                             toPort->dataType + ")");
            } else {
                wiredInputs.insert(connections[i].to);
            }
        }
    }

    for (const auto& kv : systemPorts.inputs) {
        std::string path = "$.systemPorts.inputs." + kv.first;
        const PortDescriptor* p = resolvePort(kv.second, path);
        if (p) {
            if (p->direction != PortDescriptor::Direction::Input) {
                addError(local, path, SpecErrorCode::BAD_REFERENCE,
                         "'" + kv.second + "' is not an input port");
            } else {
                wiredInputs.insert(kv.second);
            }
        }
    }
    for (const auto& kv : systemPorts.outputs) {
        std::string path = "$.systemPorts.outputs." + kv.first;
        const PortDescriptor* p = resolvePort(kv.second, path);
        if (p && p->direction != PortDescriptor::Direction::Output) {
            addError(local, path, SpecErrorCode::BAD_REFERENCE,
                     "'" + kv.second + "' is not an output port");
        }
    }

    for (size_t i = 0; i < units.size(); ++i) {
        const UnitInfo& info = unitsByName.at(units[i].name);
        if (!info.descriptor) {
            continue; // unknown type already reported
        }
        for (const auto& pd : info.descriptor->ports) {
            if (pd.direction == PortDescriptor::Direction::Input && pd.required) {
                std::string ref = units[i].name + "." + pd.name;
                if (wiredInputs.find(ref) == wiredInputs.end()) {
                    addError(local, "$.units[" + std::to_string(i) + "]",
                             SpecErrorCode::REQUIRED_INPUT_UNWIRED,
                             "required input '" + pd.name + "' on unit '" + units[i].name +
                                 "' is not wired");
                }
            }
        }
    }

    // Adapters: structural well-formedness only. Whether an instance was
    // actually injected (addAdapter, before loadFromSpec) is a runtime
    // concern the future loader checks (SpecErrorCode::MISSING_ADAPTER,
    // design spec §4.2 step 3) — out of scope for a zero-instantiation pass.
    for (size_t i = 0; i < adapters.size(); ++i) {
        std::string base = "$.adapters[" + std::to_string(i) + "]";
        if (adapters[i].uri.find("://") == std::string::npos) {
            addError(local, base + ".uri", SpecErrorCode::BAD_REFERENCE,
                     "uri '" + adapters[i].uri + "' is not well-formed (expected 'scheme://...')");
        }
    }

    errors.insert(errors.end(), local.begin(), local.end());
    return local.empty();
}

SystemConfiguration SystemSpec::systemConfiguration() const {
    SystemConfiguration cfg;
    cfg.systemName = systemName;
    cfg.logLevel = logLevel;
    cfg.defaultSchedulingPolicy = schedulingPolicy;
    cfg.systemTickRate = tickRate;
    cfg.enableRealTimeScheduling = enableRealTimeScheduling;
    cfg.maxProcessingUnits = maxProcessingUnits;
    cfg.eventPoolSize = eventPoolSize;
    cfg.eventQueueSize = eventQueueSize;
    return cfg;
}

} // namespace axonvex::core
