#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <axonvex/core/callerKeyed.hpp>
#include <axonvex/core/callback.hpp>

namespace axonvex::interfaces {

struct ProtocolStatistics {
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
    uint64_t messagesSent{0};
    uint64_t messagesReceived{0};
    void reset() { bytesSent=bytesReceived=messagesSent=messagesReceived=0; }
};

// Transport-agnostic protocol interface
class ProtocolInterface : public axonvex::core::CallerKeyed<std::string, std::vector<uint8_t>> {
  public:
    virtual ~ProtocolInterface() = default;

    // Lifecycle
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    // Send/Receive
    virtual bool send(const std::vector<uint8_t>& data) = 0;

    using MessageCallback = axonvex::core::Callback<std::vector<uint8_t>>;

    // Convenience single-channel setter (registers under a default key)
    virtual void setMessageCallback(MessageCallback* cb) = 0;

    // Keyed subscription API for messages
    virtual void registerMessageHandler(const std::string& key, MessageCallback* cb) = 0;
    virtual bool unregisterMessageHandler(const std::string& key, MessageCallback* cb) = 0;
    virtual size_t unregisterAllMessageHandlersForKey(const std::string& key) = 0;

    // Error handling
    using ErrorCallback = std::function<void(const std::string&)>; // convenience
    using ErrorHandler = axonvex::core::Callback<std::string>;     // keyed handlers
    virtual void setErrorCallback(ErrorCallback cb) = 0;           // registers under default key
    virtual void registerErrorHandler(const std::string& key, ErrorHandler* cb) = 0;
    virtual bool unregisterErrorHandler(const std::string& key, ErrorHandler* cb) = 0;
    virtual size_t unregisterAllErrorHandlersForKey(const std::string& key) = 0;

    // Configuration
    virtual bool configure(const std::string& key, const std::string& value) = 0;

    // Monitoring
    virtual ProtocolStatistics getStatistics() const = 0;
};

} // namespace axonvex::interfaces
