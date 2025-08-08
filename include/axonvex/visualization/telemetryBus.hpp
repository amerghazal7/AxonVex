#pragma once

#include <axonvex/core/callerKeyed.hpp>
#include <axonvex/core/callback.hpp>
#include <string>
#include <vector>

namespace axonvex::visualization {

class TelemetryBus : public axonvex::core::CallerKeyed<std::string, std::vector<uint8_t>> {
  public:
    using Message = std::vector<uint8_t>;

    void publish(const std::string& channel, const Message& data) {
        // Fan-out to subscribers for the given channel
        this->callCallbacksByKey(channel, data);
        // Optional: also publish to a wildcard channel "*" if subscribers exist
        this->callCallbacksByKey("*", data);
    }

    using Subscriber = axonvex::core::Callback<Message>;
    void subscribe(const std::string& channel, Subscriber* cb) {
        this->registerKeyedCallback(channel, cb);
    }
    bool unsubscribe(const std::string& channel, Subscriber* cb) {
        return this->unregisterKeyedCallback(channel, cb);
    }
    size_t unsubscribeAll(const std::string& channel) {
        return this->unregisterAllCallbacksForKey(channel);
    }
};

} // namespace axonvex::visualization
