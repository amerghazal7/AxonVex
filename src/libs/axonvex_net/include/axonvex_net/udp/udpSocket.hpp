#pragma once

#include <atomic>
#include <axonvex_interfaces/detail/dispatchBarrier.hpp>
#include <axonvex_interfaces/protocolInterface.hpp>
#include <axonvex_net/detail/addressResolver.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace axonvex {
namespace interfaces {
namespace udp {

/// UDP needs no framing header: each datagram is exactly one message frame
/// (contrast TcpClient's magic+length wire format, C11).
class UdpSocket : public axonvex::interfaces::ProtocolInterface {
  public:
    explicit UdpSocket(std::string bindAddress = "0.0.0.0", uint16_t port = 9001);

    /// Without this, destroying a still-running socket runs ~std::thread on a
    /// joinable thread, which calls std::terminate.
    ~UdpSocket() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;
    bool send(const std::vector<uint8_t>& data) override;

    using ProtocolInterface::ErrorCallback;
    using ProtocolInterface::ErrorHandler;
    using ProtocolInterface::MessageCallback;

    void setMessageCallback(MessageCallback* cb) override;
    void registerMessageHandler(const std::string& key, MessageCallback* cb) override;
    bool unregisterMessageHandler(const std::string& key, MessageCallback* cb) override;
    size_t unregisterAllMessageHandlersForKey(const std::string& key) override;

    void setErrorCallback(ErrorCallback cb) override;
    void registerErrorHandler(const std::string& key, ErrorHandler* cb) override;
    bool unregisterErrorHandler(const std::string& key, ErrorHandler* cb) override;
    size_t unregisterAllErrorHandlersForKey(const std::string& key) override;

    /// Setup only: bindAddress_/port_/remoteHost_/remotePort_ are read unguarded
    /// by openAndBind()/updateRemote(), so this must complete before start() and
    /// must not run concurrently with itself. (The resolved address it produces
    /// *is* guarded — peerMutex_ covers remoteAddr_/remoteSet_.)
    bool configure(const std::string& key, const std::string& value) override;

    ProtocolStatistics getStatistics() const override;

  private:
    static constexpr const char* defaultKey() {
        return "default";
    }

    /// Upper bound on how long stop() waits for the receive thread to notice it.
    static constexpr int RECV_TIMEOUT_MS = 100;

#if defined(AXONVEX_PLATFORM_LINUX)
    /// Appends failure messages to @p errorsOut instead of dispatching them:
    /// it runs under lifecycleMutex_ (from start()), and user callbacks must
    /// never be invoked while a transport lock is held (C34/C12).
    bool openAndBind(std::vector<std::string>& errorsOut);

    /// Called twice on purpose when remote_host is configured before start():
    /// once from configure(), when sockFamily_ is still AF_UNSPEC and the family
    /// preference cannot be applied, and again from openAndBind() once the
    /// socket's real family is known, which overwrites that first guess.
    /// Appends failures to @p errorsOut (same rationale as openAndBind).
    void updateRemote(std::vector<std::string>& errorsOut);

    /// configure()-path wrapper: collects from updateRemote() and dispatches
    /// immediately — safe there because configure() holds no transport lock.
    void updateRemoteAndReport();

    void recvLoop();
#endif

    /// Outside the platform guard: the non-Linux build needs it to report that
    /// it cannot do the operation at all.
    /// Same contract as reportError: user code never runs under cbMutex_ (C34).
    void dispatchMessage(const std::vector<uint8_t>& data);

    /// Snapshot under cbMutex_, dispatch outside it (C34). The barrier holds off
    /// any unregister from another thread until this dispatch drains, so the raw
    /// handler pointers cannot be torn down mid-call.
    void reportError(const std::string& msg);

    std::string bindAddress_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    /// Serialises start()/stop() so only one caller ever tears the thread down.
    mutable std::mutex lifecycleMutex_;
    /// Published by the worker itself; see stop().
    std::atomic<std::thread::id> workerId_{std::thread::id()};

    MessageCallback* defaultMsgCb_{nullptr};

    axonvex::core::CallerKeyed<std::string, std::string> errorKeyed_;
    std::unique_ptr<ErrorHandler> errorAdapter_;

    mutable std::mutex cbMutex_;
    /// Keeps handler pointers alive across an unlocked dispatch (C34).
    detail::DispatchBarrier dispatch_;
    AtomicProtocolStatistics stats_{};

    // Remote destination configuration
    std::string remoteHost_;
    uint16_t remotePort_{0};

    /// Guards sock_ against being closed while send() is inside sendto().
    mutable std::mutex sockMutex_;
    /// Guards the resolved peer/remote addresses. It exists for the steady-state
    /// race — recvLoop() writes lastPeer_ on every datagram while send() reads
    /// it — NOT to make configure() a runtime-reconfiguration API. configure()
    /// is setup-only (see its comment); it resolves through getaddrinfo, which
    /// blocks on DNS for as long as the system resolver takes.
    mutable std::mutex peerMutex_;

#if defined(AXONVEX_PLATFORM_LINUX)
    int sock_{-1};
    /// Family the socket was actually bound with; destinations must match it.
    int sockFamily_{AF_UNSPEC};
    detail::ResolvedAddress remoteAddr_{};
    bool remoteSet_{false};
    detail::ResolvedAddress lastPeer_{};
    bool lastPeerSet_{false};
#endif
};

} // namespace udp
} // namespace interfaces
} // namespace axonvex
