/**
 * @file unitDescriptorHonestyCheck.hpp
 * @brief Reusable test helper: does a UnitTypeDescriptor match what the unit
 *        actually builds? (design spec §3.4 "Honesty check", CLAUDE.md rule 7.)
 *
 * Metadata that lies is worse than no metadata: the composer generates
 * wiring from it. Every new built-in unit type's test should run its
 * descriptor + a live instance through verifyDescriptorMatchesInstance() and
 * assert the result is empty.
 */

#pragma once

#include <algorithm>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/unitMetadata.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace axonvex::core::testing {

/// Bridges the two type-identity spaces that coexist this wave: the
/// descriptor's canonical name (design spec §3.2) and BasePort's mangled
/// typeid name (unchanged — migrating BasePort itself is Track PORTS'
/// territory, a later wave). Only covers the primitives unitMetadata.hpp
/// pre-registers; an unrecognized canonical name returns an empty string and
/// the caller skips the dataType assertion for that port rather than
/// silently declaring it correct.
inline std::string knownTypeIdNameFor(const std::string& canonical) {
    if (canonical == "bool")
        return typeid(bool).name();
    if (canonical == "int32")
        return typeid(std::int32_t).name();
    if (canonical == "int64")
        return typeid(std::int64_t).name();
    if (canonical == "double")
        return typeid(double).name();
    if (canonical == "string")
        return typeid(std::string).name();
    return std::string();
}

/// Returns human-readable mismatches; empty means the descriptor is honest.
inline std::vector<std::string> verifyDescriptorMatchesInstance(const UnitTypeDescriptor& meta,
                                                                const ProcessingUnit& instance) {
    std::vector<std::string> issues;

    auto groupFor = [&](PortDescriptor::Direction dir,
                        PortDescriptor::Kind kind) -> const std::map<int, BasePort*>& {
        if (dir == PortDescriptor::Direction::Input) {
            return kind == PortDescriptor::Kind::Sync ? instance.getInputPorts()
                                                      : instance.getAsyncInputPorts();
        }
        return kind == PortDescriptor::Kind::Sync ? instance.getOutputPorts()
                                                  : instance.getAsyncOutputPorts();
    };

    // Declared -> actually created.
    for (const auto& d : meta.ports) {
        const auto& group = groupFor(d.direction, d.kind);
        auto it = group.find(d.index);
        if (it == group.end()) {
            issues.push_back("descriptor declares port '" + d.name + "' (index " +
                             std::to_string(d.index) + ") that the instance never created");
            continue;
        }
        BasePort* port = it->second;
        if (port->getName() != d.name) {
            issues.push_back("port index " + std::to_string(d.index) +
                             " name mismatch: descriptor='" + d.name + "' instance='" +
                             port->getName() + "'");
        }
        std::string expectedTypeId = knownTypeIdNameFor(d.dataType);
        if (!expectedTypeId.empty() && expectedTypeId != port->getDataTypeName()) {
            issues.push_back("port '" + d.name + "' dataType mismatch: descriptor claims '" +
                             d.dataType + "' but the instance's port is a different type");
        }
    }

    // Actually created -> declared (skip the framework-injected control ports;
    // those are not part of a unit type's declared contract).
    auto checkActual = [&](PortDescriptor::Direction dir, PortDescriptor::Kind kind) {
        for (const auto& kv : groupFor(dir, kind)) {
            if (kv.first == ControlPorts::RESET || kv.first == ControlPorts::DISABLE) {
                continue;
            }
            bool declared =
                std::any_of(meta.ports.begin(), meta.ports.end(), [&](const PortDescriptor& d) {
                    return d.direction == dir && d.kind == kind && d.index == kv.first;
                });
            if (!declared) {
                issues.push_back("instance has undeclared port '" + kv.second->getName() +
                                 "' (index " + std::to_string(kv.first) + ")");
            }
        }
    };
    checkActual(PortDescriptor::Direction::Input, PortDescriptor::Kind::Sync);
    checkActual(PortDescriptor::Direction::Output, PortDescriptor::Kind::Sync);
    checkActual(PortDescriptor::Direction::Input, PortDescriptor::Kind::Async);
    checkActual(PortDescriptor::Direction::Output, PortDescriptor::Kind::Async);

    return issues;
}

} // namespace axonvex::core::testing
