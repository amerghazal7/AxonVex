/**
 * @file unitMetadata.cpp
 * @brief Introspection metadata contract implementation.
 */

#include "axonvex_core/unitMetadata.hpp"

namespace axonvex::core {

namespace {

const char* directionName(PortDescriptor::Direction d) {
    return d == PortDescriptor::Direction::Input ? "Input" : "Output";
}

const char* kindName(PortDescriptor::Kind k) {
    return k == PortDescriptor::Kind::Sync ? "Sync" : "Async";
}

const char* paramTypeName(ParamDescriptor::Type t) {
    switch (t) {
        case ParamDescriptor::Type::Bool:
            return "Bool";
        case ParamDescriptor::Type::Int:
            return "Int";
        case ParamDescriptor::Type::Double:
            return "Double";
        case ParamDescriptor::Type::String:
            return "String";
        case ParamDescriptor::Type::Enum:
            return "Enum";
        case ParamDescriptor::Type::Object:
            return "Object";
    }
    return "String"; // unreachable; keeps -Wreturn-type quiet across compilers
}

/// True if `value`'s JSON runtime type is compatible with `type`. Double
/// accepts JSON integers too (a numeric literal like `1` is a valid double).
bool jsonTypeMatches(ParamDescriptor::Type type, const nlohmann::json& value) {
    switch (type) {
        case ParamDescriptor::Type::Bool:
            return value.is_boolean();
        case ParamDescriptor::Type::Int:
            return value.is_number_integer();
        case ParamDescriptor::Type::Double:
            return value.is_number();
        case ParamDescriptor::Type::String:
        case ParamDescriptor::Type::Enum:
            return value.is_string();
        case ParamDescriptor::Type::Object:
            return value.is_object();
    }
    return false;
}

/// Best-effort min/max/values constraint check. Absent or malformed
/// constraints are treated as "no constraint" — the descriptor is trusted
/// (it is authored by the unit type's own code, not untrusted spec input);
/// this only checks values that flow FROM the spec document.
bool constraintsSatisfied(const ParamDescriptor& desc, const nlohmann::json& value,
                          std::string& violation) {
    if (!desc.constraints.is_object()) {
        return true;
    }
    if (desc.type == ParamDescriptor::Type::Enum) {
        auto valuesIt = desc.constraints.find("values");
        if (valuesIt != desc.constraints.end() && valuesIt->is_array()) {
            for (const auto& allowed : *valuesIt) {
                if (allowed == value) {
                    return true;
                }
            }
            violation = "value '" + value.dump() + "' is not one of the allowed enum values";
            return false;
        }
        return true; // no values list to check against
    }
    if (desc.type == ParamDescriptor::Type::Int || desc.type == ParamDescriptor::Type::Double) {
        if (!value.is_number()) {
            return true; // type mismatch already reported separately
        }
        double v = value.get<double>();
        auto minIt = desc.constraints.find("min");
        if (minIt != desc.constraints.end() && minIt->is_number() && v < minIt->get<double>()) {
            violation = "value " + value.dump() + " is below the minimum " + minIt->dump();
            return false;
        }
        auto maxIt = desc.constraints.find("max");
        if (maxIt != desc.constraints.end() && maxIt->is_number() && v > maxIt->get<double>()) {
            violation = "value " + value.dump() + " exceeds the maximum " + maxIt->dump();
            return false;
        }
    }
    return true;
}

} // namespace

nlohmann::json PortDescriptor::toJson() const {
    return nlohmann::json{
        {"index", index},         {"name", name},         {"direction", directionName(direction)},
        {"kind", kindName(kind)}, {"dataType", dataType}, {"required", required}};
}

nlohmann::json ParamDescriptor::toJson() const {
    return nlohmann::json{{"name", name},
                          {"type", paramTypeName(type)},
                          {"defaultValue", defaultValue},
                          {"description", description},
                          {"constraints", constraints}};
}

nlohmann::json UnitTypeDescriptor::toJson() const {
    nlohmann::json portsJson = nlohmann::json::array();
    for (const auto& p : ports) {
        portsJson.push_back(p.toJson());
    }
    nlohmann::json paramsJson = nlohmann::json::array();
    for (const auto& p : parameters) {
        paramsJson.push_back(p.toJson());
    }
    return nlohmann::json{{"typeName", typeName},
                          {"description", description},
                          {"ports", portsJson},
                          {"parameters", paramsJson}};
}

std::vector<ParamValidationIssue> validateParams(const UnitTypeDescriptor& meta,
                                                 const nlohmann::json& params) {
    std::vector<ParamValidationIssue> issues;

    nlohmann::json obj = params.is_null() ? nlohmann::json::object() : params;
    if (!obj.is_object()) {
        issues.push_back({"", "TYPE_MISMATCH", "params must be a JSON object"});
        return issues;
    }

    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool known = false;
        for (const auto& desc : meta.parameters) {
            if (desc.name == it.key()) {
                known = true;
                break;
            }
        }
        if (!known) {
            issues.push_back(
                {it.key(), "UNKNOWN_KEY",
                 "unit type '" + meta.typeName + "' has no parameter named '" + it.key() + "'"});
        }
    }

    for (const auto& desc : meta.parameters) {
        auto found = obj.find(desc.name);
        if (found == obj.end()) {
            if (desc.isRequired()) {
                issues.push_back({desc.name, "MISSING_REQUIRED",
                                  "required parameter '" + desc.name + "' is missing"});
            }
            continue;
        }
        if (!jsonTypeMatches(desc.type, *found)) {
            issues.push_back({desc.name, "TYPE_MISMATCH",
                              "parameter '" + desc.name + "' expects type " +
                                  paramTypeName(desc.type) + ", got " + found->type_name()});
            continue;
        }
        std::string violation;
        if (!constraintsSatisfied(desc, *found, violation)) {
            issues.push_back(
                {desc.name, "CONSTRAINT_VIOLATION", "parameter '" + desc.name + "': " + violation});
        }
    }

    return issues;
}

} // namespace axonvex::core
