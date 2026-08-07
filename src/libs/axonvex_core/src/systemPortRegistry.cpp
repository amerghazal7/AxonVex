#include <axonvex_core/ports.hpp>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/systemPortRegistry.hpp>

namespace axonvex::core {

bool SystemPortRegistry::assignInput(const std::string& name, BasePort* port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (inputs_.find(name) != inputs_.end()) {
        return false;
    }
    inputs_[name] = port;
    return true;
}

bool SystemPortRegistry::assignOutput(const std::string& name, BasePort* port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outputs_.find(name) != outputs_.end()) {
        return false;
    }
    outputs_[name] = port;
    return true;
}

bool SystemPortRegistry::removeInput(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = inputs_.find(name);
    if (it == inputs_.end()) {
        return false;
    }
    inputs_.erase(it);
    return true;
}

bool SystemPortRegistry::removeOutput(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = outputs_.find(name);
    if (it == outputs_.end()) {
        return false;
    }
    outputs_.erase(it);
    return true;
}

BasePort* SystemPortRegistry::input(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = inputs_.find(name);
    return it != inputs_.end() ? it->second : nullptr;
}

BasePort* SystemPortRegistry::output(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = outputs_.find(name);
    return it != outputs_.end() ? it->second : nullptr;
}

bool SystemPortRegistry::hasInput(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inputs_.find(name) != inputs_.end();
}

bool SystemPortRegistry::hasOutput(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outputs_.find(name) != outputs_.end();
}

std::vector<std::string> SystemPortRegistry::inputNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(inputs_.size());
    for (const auto& pair : inputs_) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<std::string> SystemPortRegistry::outputNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(outputs_.size());
    for (const auto& pair : outputs_) {
        names.push_back(pair.first);
    }
    return names;
}

size_t SystemPortRegistry::inputCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return inputs_.size();
}

size_t SystemPortRegistry::outputCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return outputs_.size();
}

void SystemPortRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    inputs_.clear();
    outputs_.clear();
}

std::vector<SystemPortRegistry::PortDescription> SystemPortRegistry::describeInputs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PortDescription> described;
    described.reserve(inputs_.size());
    for (const auto& pair : inputs_) {
        PortDescription desc;
        desc.name = pair.first;
        if (pair.second) {
            desc.dataTypeName = pair.second->getDataTypeName();
            ProcessingUnit* owner = pair.second->getOwner();
            if (owner) {
                desc.ownerName = owner->getName();
            }
        }
        described.push_back(std::move(desc));
    }
    return described;
}

std::vector<SystemPortRegistry::PortDescription> SystemPortRegistry::describeOutputs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PortDescription> described;
    described.reserve(outputs_.size());
    for (const auto& pair : outputs_) {
        PortDescription desc;
        desc.name = pair.first;
        if (pair.second) {
            desc.dataTypeName = pair.second->getDataTypeName();
            ProcessingUnit* owner = pair.second->getOwner();
            if (owner) {
                desc.ownerName = owner->getName();
            }
        }
        described.push_back(std::move(desc));
    }
    return described;
}

SystemPortRegistry::RemovedPorts SystemPortRegistry::removeAllForOwner(ProcessingUnit* owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    RemovedPorts removed;

    auto inputIt = inputs_.begin();
    while (inputIt != inputs_.end()) {
        if (inputIt->second && inputIt->second->getOwner() == owner) {
            removed.inputs.push_back(inputIt->first);
            inputIt = inputs_.erase(inputIt);
        } else {
            ++inputIt;
        }
    }

    auto outputIt = outputs_.begin();
    while (outputIt != outputs_.end()) {
        if (outputIt->second && outputIt->second->getOwner() == owner) {
            removed.outputs.push_back(outputIt->first);
            outputIt = outputs_.erase(outputIt);
        } else {
            ++outputIt;
        }
    }

    return removed;
}

std::pair<std::unique_lock<std::mutex>, std::unique_lock<std::mutex>> SystemPortRegistry::lockWith(
    SystemPortRegistry& target) {
    std::unique_lock<std::mutex> lock1(mutex_, std::defer_lock);
    std::unique_lock<std::mutex> lock2;
    if (&target == this) {
        lock1.lock();
    } else {
        lock2 = std::unique_lock<std::mutex>(target.mutex_, std::defer_lock);
        std::lock(lock1, lock2);
    }
    return std::make_pair(std::move(lock1), std::move(lock2));
}

} // namespace axonvex::core
