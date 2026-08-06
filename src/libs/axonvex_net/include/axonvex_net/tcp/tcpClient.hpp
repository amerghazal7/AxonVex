#pragma once

#include <atomic>
#include <axonvex_interfaces/detail/dispatchBarrier.hpp>
#include <axonvex_interfaces/protocolInterface.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <sys/types.h> // ssize_t
#endif

namespace axonvex {
namespace interfaces {
namespace tcp {

/// C11 wire format: every frame on a TCP connection is a 6-byte header —
/// magic bytes 0xAF 0x01, then the payload length as a big-endian uint32 —
/// followed by exactly that many payload bytes. Zero-length payloads are
/// legal frames. The magic makes a protocol mismatch (old newline peer,
/// non-AxonVex peer) a precise error instead of a misleading giant-length
/// error; a future format revision changes the magic.
constexpr uint8_t kFrameMagic0 = 0xAF;
constexpr uint8_t kFrameMagic1 = 0x01;
constexpr size_t kFrameHeaderSize = 6;
/// Trust-boundary guard: a corrupt or hostile length field must not drive a
/// multi-gigabyte allocation. Ceiling: 16 MiB per frame; upgrade path is a
/// ProtocolConfiguration field if a real consumer ever needs bigger.
constexpr uint32_t kMaxFrameLength = 16u * 1024u * 1024u;

class TcpClient : public axonvex::interfaces::ProtocolInterface {
  public:
    explicit TcpClient(std::string host = "127.0.0.1", uint16_t port = 9000);

    /// Without this, destroying a still-running client runs ~std::thread on a
    /// joinable thread, which calls std::terminate.
    ~TcpClient() override;

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

    /// Setup only: host_/port_ are read unguarded by connectSocket(), so this
    /// must complete before start() and must not run concurrently with itself.
    bool configure(const std::string& key, const std::string& value) override;

    ProtocolStatistics getStatistics() const override;

  private:
    static constexpr const char* defaultKey() {
        return "default";
    }

    /// Upper bound on how long stop() waits for the receive thread to notice it.
    static constexpr int RECV_TIMEOUT_MS = 100;

#if defined(AXONVEX_PLATFORM_LINUX)
    bool connectSocket();

    /// ::send may accept fewer bytes than asked; the caller must resume from the
    /// offset. Not looping here silently truncated the payload and still counted
    /// the message as sent (C11).
    bool sendAll(const uint8_t* bytes, size_t length, ssize_t& sentOut);

    /// Requires sockMutex_ held. Marks the stream dead after a failed frame
    /// write (no resync point on TCP) and shutdown()s the socket so recvLoop
    /// wakes, exits, and becomes reapable by the next start().
    void poisonConnectionLocked();

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

    std::string host_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    /// Liveness of the current connection, distinct from running_ on purpose:
    /// running_ is the caller's intent (set by start(), cleared by stop()),
    /// connectionAlive_ is the stream's actual state — cleared by recvLoop on
    /// any terminal exit (framing violation, peer EOF, recv error) and by a
    /// failed frame write in send(). It cannot be folded into running_:
    /// clearing running_ from inside recvLoop would let a concurrent start()
    /// see "not running" while this worker's handle is still joinable and
    /// move-assign over it, which is std::terminate. start() keys off this
    /// flag instead and joins the dead worker before reassigning.
    std::atomic<bool> connectionAlive_{false};
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

    /// Guards sock_ against being closed while send() is inside ::send().
    mutable std::mutex sockMutex_;

#if defined(AXONVEX_PLATFORM_LINUX)
    int sock_{-1};
#endif
};

} // namespace tcp
} // namespace interfaces
} // namespace axonvex
