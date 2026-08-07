#pragma once

#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_core/callerKeyed.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace axonvex::interfaces {

/// Snapshot of a transport's counters, returned by value to callers.
struct ProtocolStatistics {
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
    uint64_t messagesSent{0};
    uint64_t messagesReceived{0};
    void reset() {
        bytesSent = bytesReceived = messagesSent = messagesReceived = 0;
    }
};

/// Live counters for a transport. Send and receive run on different threads, so
/// the counters a transport increments must be atomic even though the snapshot
/// handed back to callers is plain (C24).
///
/// Relaxed ordering throughout: these count events, they order nothing. A
/// snapshot is therefore per-counter atomic but not consistent across all four
/// — a reader can catch bytesSent updated and messagesSent not yet. That is
/// acceptable for statistics and must not be used to derive control decisions.
struct AtomicProtocolStatistics {
    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<uint64_t> messagesSent{0};
    std::atomic<uint64_t> messagesReceived{0};

    ProtocolStatistics snapshot() const noexcept {
        ProtocolStatistics out;
        out.bytesSent = bytesSent.load(std::memory_order_relaxed);
        out.bytesReceived = bytesReceived.load(std::memory_order_relaxed);
        out.messagesSent = messagesSent.load(std::memory_order_relaxed);
        out.messagesReceived = messagesReceived.load(std::memory_order_relaxed);
        return out;
    }

    void reset() noexcept {
        bytesSent.store(0, std::memory_order_relaxed);
        bytesReceived.store(0, std::memory_order_relaxed);
        messagesSent.store(0, std::memory_order_relaxed);
        messagesReceived.store(0, std::memory_order_relaxed);
    }
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
