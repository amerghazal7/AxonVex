/**
 * @file ports.hpp
 * @brief Advanced Port System for AxonVex Framework
 * @author AxonVex Development Team
 * @version 2.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * Advanced port system integer indexing,
 * async ports, and advanced connection patterns while maintaining
 * AxonVex's modern C++ design principles.
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace axonvex::core {

// Forward declarations
class ProcessingUnit;

/**
 * @brief Port types for port system
 */
enum class PortType { SYNC_INPUT, SYNC_OUTPUT, ASYNC_INPUT, ASYNC_OUTPUT };

/**
 * @brief Base port class for the port system
 */
class BasePort {
  public:
    explicit BasePort(int id, const std::string& name, PortType type, ProcessingUnit* owner);
    virtual ~BasePort() = default;

    // Port identification
    int getId() const noexcept {
        return id_;
    }
    const std::string& getName() const noexcept {
        return name_;
    }
    PortType getType() const noexcept {
        return type_;
    }
    ProcessingUnit* getOwner() const noexcept {
        return owner_;
    }

    // Port UID generation (block_uid*256 + port_idx)
    uint32_t getPortUID() const noexcept;
    void setPortUID(uint32_t uid) noexcept {
        portUID_ = uid;
    }

    // Type information
    virtual std::string getDataTypeName() const = 0;
    virtual size_t getDataTypeSize() const = 0;

    // Statistics. V14: relaxed — these are pure diagnostic counters with no
    // ordering role (nothing uses them to publish/acquire other state), so the
    // seq_cst default bought nothing but the fence cost on every message.
    uint64_t getTotalMessages() const noexcept {
        return totalMessages_.load(std::memory_order_relaxed);
    }
    uint64_t getValidMessages() const noexcept {
        return validMessages_.load(std::memory_order_relaxed);
    }
    uint64_t getInvalidMessages() const noexcept {
        return invalidMessages_.load(std::memory_order_relaxed);
    }

    // Debug and validation
    void setDescription(const std::string& desc) {
        description_ = desc;
    }
    const std::string& getDescription() const noexcept {
        return description_;
    }

    // Thread safety is fixed at construction and cannot be changed afterwards.
    //
    // It used to be settable, which was unsound: the flag selects between a
    // locked and an unlocked access discipline, so flipping it while producers
    // and consumers were live let a writer that read `true` take the mutex path
    // concurrently with a reader that read `false` taking the unlocked one, both
    // on the same payload. An atomic flag makes the read safe, not the switch.
    // Pass the mode to the port's constructor instead (C31).
    virtual bool isThreadSafe() const noexcept = 0;

    // Reset functionality
    virtual void reset() = 0;

    /// Type-erased connect/disconnect for callers (the .axv.json spec loader)
    /// that hold a BasePort* and not the template type. Returns false on a
    /// kind/type mismatch instead of throwing or silently wiring a type-punned
    /// connection -- the caller (SpecSystem::initializeBlocksLayout, design
    /// spec §3.5/§4.2 step 4) turns false into a validation error or, if it
    /// reaches this after its own pre-flight validation already passed, a
    /// fatal defect. OutputPort<T>/AsyncOutputPort<T> override via
    /// dynamic_cast<InputPort<T>*>/<AsyncInputPort<T>*> below; the default
    /// here (input ports are never the connecting side) stays a refusal.
    /// Setup-time only -- never called from the data path.
    virtual bool tryConnect(BasePort* /*target*/) {
        return false;
    }
    virtual bool tryDisconnect(BasePort* /*target*/) {
        return false;
    }

  protected:
    int id_;
    std::string name_;
    std::string description_;
    PortType type_;
    ProcessingUnit* owner_;
    uint32_t portUID_{0};

    // Statistics
    std::atomic<uint64_t> totalMessages_{0};
    std::atomic<uint64_t> validMessages_{0};
    std::atomic<uint64_t> invalidMessages_{0};

    // Validation helpers
    template <typename T>
    bool validateData(const T& data);

    /// Reject a callback registration once the port has carried traffic (C32).
    ///
    /// The dispatch path reads these `std::function` members with no lock, and
    /// it must stay that way: a lock there would run user code under a port
    /// lock (the C12/C18 bug class), and copying the function out to call it
    /// unlocked would allocate on the port hot path. Both are banned. So
    /// registration is setup-only — and rather than leave that as a comment
    /// nobody reads, a late registration fails loudly here instead of silently
    /// racing a concurrent dispatch. Costs nothing on the hot path: setters are
    /// not hot.
    ///
    /// Two limits this deliberately does not close, so it is not mistaken for
    /// more than it is. The load is relaxed, same as the increment (V14 — these
    /// are pure counters with no ordering role to preserve), so a setter racing
    /// the *very first* dispatch can still read 0 and slip through — that is
    /// already a violation of the setup-only contract, which no runtime check
    /// can repair. And the trigger is "the port carried a message", not "this
    /// particular callback was read", so a message rejected by NaN validation
    /// before the callback is ever consulted still latches the port. Erring
    /// toward rejection is the safe direction.
    void requireNoTrafficYet(const char* what) const {
        if (totalMessages_.load(std::memory_order_relaxed) != 0) {
            throw std::logic_error(std::string(what) +
                                   " must be called before the port carries traffic: the dispatch "
                                   "path reads callbacks unlocked, so late registration would race "
                                   "it (port '" +
                                   name_ + "')");
        }
    }

    // V14: relaxed fetch_add — pure counters, no thread uses them to order
    // access to any other field (payload visibility is carried by hasNewData_/
    // wasUpdated_ and, for threadSafe ports, dataMutex_; these three are read
    // only for diagnostics via the getters above, which are relaxed to match).
    void incrementTotalMessages() {
        totalMessages_.fetch_add(1, std::memory_order_relaxed);
    }
    void incrementValidMessages() {
        validMessages_.fetch_add(1, std::memory_order_relaxed);
    }
    void incrementInvalidMessages() {
        invalidMessages_.fetch_add(1, std::memory_order_relaxed);
    }
};

/**
 * @brief Synchronous Input Port
 */
template <typename T>
class InputPort : public BasePort {
  public:
    using ValidationCallback = std::function<bool(const T&)>;
    using DataCallback = std::function<void(const T&)>;

    explicit InputPort(int id, const std::string& name, ProcessingUnit* owner,
                       bool threadSafe = false);
    ~InputPort() override = default;

    // Data access
    T read() const;
    bool hasNewData() const noexcept;
    void clearNewDataFlag() noexcept;

    // Connection management (called by OutputPort)
    void writeData(const T& data);

    // Callbacks and validation
    void setValidationCallback(ValidationCallback callback);
    void setDataCallback(DataCallback callback);

    // Bridge ports for complex routing
    void addBridgedPort(InputPort<T>* bridgedPort);
    void removeBridgedPort(InputPort<T>* bridgedPort);
    bool isBridgePort() const noexcept;

    // Port information
    std::string getDataTypeName() const override {
        return typeid(T).name();
    }
    size_t getDataTypeSize() const override {
        return sizeof(T);
    }

    // Thread safety
    bool isThreadSafe() const noexcept override {
        return isThreadSafe_;
    }

    // Reset
    void reset() override;

  private:
    mutable std::mutex dataMutex_;
    T data_;
    std::atomic<bool> hasNewData_{false};
    const bool isThreadSafe_;

    // Callbacks
    ValidationCallback validationCallback_;
    DataCallback dataCallback_;

    // Bridge port management (V9/C35): copy-on-write, same recipe as
    // OutputPort::connections_ above. writeData() used to hold bridgeMutex_ for
    // the ENTIRE fan-out loop below, including the nested bridgedPort->writeData()
    // call — which runs that port's dataCallback_, a user callback. That is the
    // C12/C18 bug class (user code under a lock) reachable any time bridges are
    // live. Now: snapshot the list under the lock (one refcount bump, no
    // allocation), release, dispatch off the immutable snapshot. isBridgePort_ is
    // gone as a separate field — it was a plain bool written under bridgeMutex_
    // but read with no synchronization at all from writeData(), a second latent
    // race; "is a bridge port" is now just "the snapshot is non-empty".
    using BridgeList = std::vector<InputPort<T>*>;
    using BridgeSnapshot = std::shared_ptr<const BridgeList>;

    BridgeSnapshot snapshotBridgedPorts() const {
        std::lock_guard<std::mutex> lock(bridgeMutex_);
        return bridgedPorts_;
    }

    /// Caller must hold bridgeMutex_.
    BridgeList copyBridgedForUpdateLocked() const {
        return bridgedPorts_ ? BridgeList(*bridgedPorts_) : BridgeList();
    }

    mutable std::mutex bridgeMutex_;
    BridgeSnapshot bridgedPorts_;

    // NaN detection for floating point types
    bool hasNanWarned_{false};
};

/**
 * @brief Synchronous Output Port
 */
template <typename T>
class OutputPort : public BasePort {
  public:
    using OutputCallback = std::function<void(const T&)>;

    explicit OutputPort(int id, const std::string& name, ProcessingUnit* owner,
                        bool threadSafe = false);
    ~OutputPort() override = default;

    // Data output
    void write(const T& data);

    // Connection management
    void connect(InputPort<T>* inputPort);
    void disconnect(InputPort<T>* inputPort);
    void disconnectAll();
    bool isConnected() const noexcept;
    size_t getConnectionCount() const noexcept;

    /// BasePort::tryConnect/tryDisconnect override: dynamic_cast target to
    /// InputPort<T>*; false on a null cast (wrong data type) without touching
    /// connection state, true after delegating to connect()/disconnect().
    bool tryConnect(BasePort* target) override;
    bool tryDisconnect(BasePort* target) override;

    // Get connected ports
    std::vector<InputPort<T>*> getConnectedPorts() const;

    // Callbacks
    void setOutputCallback(OutputCallback callback);

    // Port information
    std::string getDataTypeName() const override {
        return typeid(T).name();
    }
    size_t getDataTypeSize() const override {
        return sizeof(T);
    }

    // Thread safety
    bool isThreadSafe() const noexcept override {
        return isThreadSafe_;
    }

    // Reset
    void reset() override;

  private:
    // Copy-on-write connection list (C35). write() must dispatch to the
    // connected ports with no lock held — those calls run user callbacks, and
    // holding connectionMutex_ across them is the C12/C18 bug class. Copying the
    // vector per write would fix that but allocates on the port path, which is
    // also banned. A shared_ptr snapshot costs one refcount bump on the write
    // path and keeps the list alive for the dispatch even if a concurrent
    // disconnect replaces it; the allocation moves to connect/disconnect, which
    // are setup operations.
    using ConnectionList = std::vector<InputPort<T>*>;
    using ConnectionSnapshot = std::shared_ptr<const ConnectionList>;

    /// V10 judgement call: KEEPING this mutex rather than switching to the
    /// std::atomic_load/atomic_store free-function overloads on shared_ptr.
    /// Those are valid C++14, but GCC's libstdc++ implements them (for a
    /// non-lock-free-capable shared_ptr control block, which this is) via a
    /// small fixed-size table of GLOBAL spinlocks keyed by hashing the
    /// shared_ptr's address — every unrelated atomic-shared_ptr in the process
    /// contends on the same small lock table, whereas connectionMutex_ is
    /// dedicated to this one port. Measured (scratch A/B micro-benchmark, 8
    /// reader threads doing snapshot() in a tight loop against a churner
    /// calling connect/disconnect-shaped swaps, -O2): the atomic_load/store
    /// version was ~10-15% SLOWER than this lock_guard, consistently across
    /// repeated runs, on top of trading a documented, auditable primitive for
    /// an implementation-defined one whose lock-freedom is not guaranteed
    /// anywhere in the standard. That is a lock traded for a hidden, busier
    /// lock — not an upgrade. Memory-ordering argument for the code as
    /// written: connectionMutex_ is acquired for both the write() path's
    /// snapshot copy (a shared_ptr copy, refcount increment only, no user
    /// code) and the connect()/disconnect() swap; std::mutex's acquire-on-lock
    /// / release-on-unlock give the snapshot a happens-after relationship with
    /// the most recent swap that completed before it, which is all callers
    /// need — nothing here claims lock-freedom, so there is no separate
    /// lock-free memory-ordering proof to make. See
    /// PortThreadSafetyTest.ConcurrentConnectDisconnectDuringWriteStaysConsistent
    /// (TSan-clean) for the stress coverage CLAUDE.md rule 8 requires.
    ConnectionSnapshot snapshotConnections() const {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        return connections_;
    }

    /// Caller must hold connectionMutex_.
    ConnectionList copyForUpdateLocked() const {
        return connections_ ? ConnectionList(*connections_) : ConnectionList();
    }

    mutable std::mutex connectionMutex_;
    ConnectionSnapshot connections_;
    const bool isThreadSafe_;

    // Current data for thread-safe access
    mutable std::mutex dataMutex_;
    T currentData_;

    OutputCallback outputCallback_;
    bool hasNanWarned_{false};
};

/**
 * @brief Asynchronous Input Port
 */
template <typename T>
class AsyncInputPort : public BasePort {
  public:
    using ValidationCallback = std::function<bool(const T&)>;
    using DataCallback = std::function<void(const T&)>;

    explicit AsyncInputPort(int id, const std::string& name, ProcessingUnit* owner,
                            bool threadSafe = false);
    ~AsyncInputPort() override = default;

    // Async data operations
    void update(const T& data);
    bool wasUpdated() const noexcept;
    void read(T& data);
    T read();

    // Callbacks and validation
    void setValidationCallback(ValidationCallback callback);
    void setDataCallback(DataCallback callback);

    // Bridge ports for complex routing
    void addBridgedPort(AsyncInputPort<T>* bridgedPort);
    void removeBridgedPort(AsyncInputPort<T>* bridgedPort);
    bool isBridgePort() const noexcept;

    // Port information
    std::string getDataTypeName() const override {
        return typeid(T).name();
    }
    size_t getDataTypeSize() const override {
        return sizeof(T);
    }

    // Thread safety
    bool isThreadSafe() const noexcept override {
        return isThreadSafe_;
    }

    // Reset
    void reset() override;

  private:
    mutable std::mutex dataMutex_;
    T data_;
    std::atomic<bool> wasUpdated_{false};
    const bool isThreadSafe_;

    // Callbacks
    ValidationCallback validationCallback_;
    DataCallback dataCallback_;

    // Bridge port management (V9/C35) — same COW recipe as InputPort<T> above:
    // update()'s fan-out dispatches off an immutable snapshot with no lock held,
    // since bridgedPort->update() runs the receiving port's dataCallback_.
    using BridgeList = std::vector<AsyncInputPort<T>*>;
    using BridgeSnapshot = std::shared_ptr<const BridgeList>;

    BridgeSnapshot snapshotBridgedPorts() const {
        std::lock_guard<std::mutex> lock(bridgeMutex_);
        return bridgedPorts_;
    }

    /// Caller must hold bridgeMutex_.
    BridgeList copyBridgedForUpdateLocked() const {
        return bridgedPorts_ ? BridgeList(*bridgedPorts_) : BridgeList();
    }

    mutable std::mutex bridgeMutex_;
    BridgeSnapshot bridgedPorts_;
};

/**
 * @brief Asynchronous Output Port
 */
template <typename T>
class AsyncOutputPort : public BasePort {
  public:
    using OutputCallback = std::function<void(const T&)>;

    explicit AsyncOutputPort(int id, const std::string& name, ProcessingUnit* owner,
                             bool threadSafe = false);
    ~AsyncOutputPort() override = default;

    // Async data operations
    void write(const T& data);

    // Connection management
    void connect(AsyncInputPort<T>* inputPort);
    void disconnect(AsyncInputPort<T>* inputPort);
    void disconnectAll();
    bool isConnected() const noexcept;
    size_t getConnectionCount() const noexcept;

    /// BasePort::tryConnect/tryDisconnect override -- see OutputPort<T>'s
    /// identical rationale.
    bool tryConnect(BasePort* target) override;
    bool tryDisconnect(BasePort* target) override;

    // Get connected ports
    std::vector<AsyncInputPort<T>*> getConnectedPorts() const;

    // Callbacks
    void setOutputCallback(OutputCallback callback);

    // Port information
    std::string getDataTypeName() const override {
        return typeid(T).name();
    }
    size_t getDataTypeSize() const override {
        return sizeof(T);
    }

    // Thread safety
    bool isThreadSafe() const noexcept override {
        return isThreadSafe_;
    }

    // Reset
    void reset() override;

    // Control async write logging
    void setLogAsyncWriteEvent(bool log) noexcept {
        logAsyncWriteEvent_ = log;
    }
    bool getLogAsyncWriteEvent() const noexcept {
        return logAsyncWriteEvent_;
    }

  private:
    // Copy-on-write connection list — see OutputPort for the rationale (C35).
    // This also retires C30's two-critical-section dance: the emptiness check
    // and the dispatch now read one immutable snapshot, so they cannot disagree.
    // V10: same "keep the mutex" judgement call as OutputPort::snapshotConnections
    // above — see its comment for the atomic_load/store spinlock-pool analysis,
    // the micro-benchmark result, and the memory-ordering argument, all of which
    // apply here unchanged.
    using ConnectionList = std::vector<AsyncInputPort<T>*>;
    using ConnectionSnapshot = std::shared_ptr<const ConnectionList>;

    ConnectionSnapshot snapshotConnections() const {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        return connections_;
    }

    /// Caller must hold connectionMutex_.
    ConnectionList copyForUpdateLocked() const {
        return connections_ ? ConnectionList(*connections_) : ConnectionList();
    }

    mutable std::mutex connectionMutex_;
    ConnectionSnapshot connections_;
    const bool isThreadSafe_;
    std::atomic<bool> logAsyncWriteEvent_{false};

    OutputCallback outputCallback_;
};

// Template implementations

template <typename T>
InputPort<T>::InputPort(int id, const std::string& name, ProcessingUnit* owner, bool threadSafe)
    : BasePort(id, name, PortType::SYNC_INPUT, owner), isThreadSafe_(threadSafe) {
    data_ = T{};
}

template <typename T>
T InputPort<T>::read() const {
    // V11: dataMutex_ here is the sanctioned RT-path exception (C31), not a
    // violation — threadSafe is chosen once at construction, so this is a
    // deliberate lock-per-message opt-in for ports with concurrent
    // producers/consumers. Ceiling: one mutex acquisition per read; construct
    // with threadSafe=false when the port has a single producer and consumer
    // (the common RT case) to avoid paying it.
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        return data_;
    }
    return data_;
}

template <typename T>
bool InputPort<T>::hasNewData() const noexcept {
    return hasNewData_.load();
}

template <typename T>
void InputPort<T>::clearNewDataFlag() noexcept {
    hasNewData_.store(false);
}

template <typename T>
void InputPort<T>::writeData(const T& data) {
    incrementTotalMessages();

    // Validate data if callback is set
    if (validationCallback_ && !validationCallback_(data)) {
        incrementInvalidMessages();
        return;
    }

    // Check for NaN in floating point data
    if (!validateData(data)) {
        incrementInvalidMessages();
        return;
    }

    incrementValidMessages();

    // Store data. V11: threadSafe=true's dataMutex_ (C31 sanctioned exception,
    // see read() above) is the only lock on this branch.
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = data;
    } else {
        data_ = data;
    }

    hasNewData_.store(true);

    // Call data callback if set. V13: dataCallback_ runs INLINE, on whatever
    // thread called writeData() — for a unit that writes from processSync(),
    // that thread is the RT scheduler thread. This is by design (it's the
    // dispatch model), not deferred: the callback set here inherits the full
    // RT-path invariant (no locks, no allocation, no I/O, no blocking) for as
    // long as it might run on that thread. See setDataCallback() below.
    if (dataCallback_) {
        dataCallback_(data);
    }

    // Propagate to bridged ports: snapshot under bridgeMutex_, dispatch outside
    // it (V9/C35) — bridgedPort->writeData() below runs that port's own
    // dataCallback_ (and recurses into ITS bridges), so no lock may be held
    // across it.
    BridgeSnapshot bridged = snapshotBridgedPorts();
    if (bridged) {
        for (auto* bridgedPort : *bridged) {
            if (bridgedPort) {
                bridgedPort->writeData(data);
            }
        }
    }
}

/// V13: runs on the writer's thread (whatever thread calls writeData()) —
/// which IS the RT path when a unit writes from processSync(). Your callback
/// inherits the RT invariant (no locks, no allocation, no I/O, no blocking)
/// for as long as it can run there. Deferral to another thread is out of
/// scope for v1.0 (see the plan's RT-path audit).
template <typename T>
void InputPort<T>::setValidationCallback(ValidationCallback callback) {
    requireNoTrafficYet("setValidationCallback");
    validationCallback_ = std::move(callback);
}

/// V13: same writer's-thread/RT-path contract as setValidationCallback above.
template <typename T>
void InputPort<T>::setDataCallback(DataCallback callback) {
    requireNoTrafficYet("setDataCallback");
    dataCallback_ = std::move(callback);
}

template <typename T>
bool InputPort<T>::isBridgePort() const noexcept {
    BridgeSnapshot bridged = snapshotBridgedPorts();
    return bridged && !bridged->empty();
}

template <typename T>
void InputPort<T>::addBridgedPort(InputPort<T>* bridgedPort) {
    if (!bridgedPort)
        return;

    std::lock_guard<std::mutex> lock(bridgeMutex_);
    BridgeList updated = copyBridgedForUpdateLocked();
    if (std::find(updated.begin(), updated.end(), bridgedPort) == updated.end()) {
        updated.push_back(bridgedPort);
        bridgedPorts_ = std::make_shared<const BridgeList>(std::move(updated));
    }
}

template <typename T>
void InputPort<T>::removeBridgedPort(InputPort<T>* bridgedPort) {
    std::lock_guard<std::mutex> lock(bridgeMutex_);
    BridgeList updated = copyBridgedForUpdateLocked();
    auto it = std::find(updated.begin(), updated.end(), bridgedPort);
    if (it != updated.end()) {
        updated.erase(it);
        bridgedPorts_ = std::make_shared<const BridgeList>(std::move(updated));
    }
}

template <typename T>
void InputPort<T>::reset() {
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = T{};
    } else {
        data_ = T{};
    }
    hasNewData_.store(false);
    hasNanWarned_ = false;
}

namespace detail {
template <typename U>
typename std::enable_if<std::is_floating_point<U>::value, bool>::type nanCheck(const U& data) {
    return !std::isnan(data);
}
template <typename U>
typename std::enable_if<!std::is_floating_point<U>::value, bool>::type nanCheck(const U&) {
    return true;
}
} // namespace detail

// Helper function for NaN validation
template <typename T>
bool BasePort::validateData(const T& data) {
    return detail::nanCheck(data);
}

// OutputPort template implementations
template <typename T>
OutputPort<T>::OutputPort(int id, const std::string& name, ProcessingUnit* owner, bool threadSafe)
    : BasePort(id, name, PortType::SYNC_OUTPUT, owner), isThreadSafe_(threadSafe) {
    currentData_ = T{};
}

template <typename T>
void OutputPort<T>::write(const T& data) {
    incrementTotalMessages();

    // Check for NaN in floating point data
    if (!validateData(data)) {
        if (!hasNanWarned_) {
            hasNanWarned_ = true;
            // Log NaN warning - in full implementation would use logger
        }
        return;
    }

    // Store current data for thread-safe access. V11: threadSafe=true's
    // dataMutex_ is the C31 sanctioned exception — see
    // InputPort<T>::read()'s comment for the full rationale and ceiling.
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        currentData_ = data;
    } else {
        currentData_ = data;
    }

    // Call output callback if set. V13: runs inline on write()'s caller's
    // thread — the RT scheduler thread for a unit writing from processSync().
    // See setOutputCallback() below for the stated contract.
    if (outputCallback_) {
        outputCallback_(data);
    }

    // Send to all connected input ports, with no lock held: writeData runs the
    // receiving port's user callbacks (C35).
    ConnectionSnapshot targets = snapshotConnections();
    if (!targets) {
        return;
    }
    for (auto* inputPort : *targets) {
        if (inputPort) {
            inputPort->writeData(data);
        }
    }
}

template <typename T>
void OutputPort<T>::connect(InputPort<T>* inputPort) {
    if (!inputPort)
        return;

    std::lock_guard<std::mutex> lock(connectionMutex_);
    ConnectionList updated = copyForUpdateLocked();
    if (std::find(updated.begin(), updated.end(), inputPort) == updated.end()) {
        updated.push_back(inputPort);
        connections_ = std::make_shared<const ConnectionList>(std::move(updated));
    }
}

template <typename T>
void OutputPort<T>::disconnect(InputPort<T>* inputPort) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    ConnectionList updated = copyForUpdateLocked();
    auto it = std::find(updated.begin(), updated.end(), inputPort);
    if (it != updated.end()) {
        updated.erase(it);
        connections_ = std::make_shared<const ConnectionList>(std::move(updated));
    }
}

template <typename T>
void OutputPort<T>::disconnectAll() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connections_.reset();
}

template <typename T>
bool OutputPort<T>::tryConnect(BasePort* target) {
    auto* typed = dynamic_cast<InputPort<T>*>(target);
    if (!typed) {
        return false;
    }
    connect(typed);
    return true;
}

template <typename T>
bool OutputPort<T>::tryDisconnect(BasePort* target) {
    auto* typed = dynamic_cast<InputPort<T>*>(target);
    if (!typed) {
        return false;
    }
    disconnect(typed);
    return true;
}

template <typename T>
bool OutputPort<T>::isConnected() const noexcept {
    ConnectionSnapshot targets = snapshotConnections();
    return targets && !targets->empty();
}

template <typename T>
size_t OutputPort<T>::getConnectionCount() const noexcept {
    ConnectionSnapshot targets = snapshotConnections();
    return targets ? targets->size() : 0;
}

template <typename T>
std::vector<InputPort<T>*> OutputPort<T>::getConnectedPorts() const {
    ConnectionSnapshot targets = snapshotConnections();
    return targets ? *targets : std::vector<InputPort<T>*>();
}

/// V13: runs on the writer's thread (whatever thread calls write()) — the RT
/// scheduler thread for a unit writing from processSync(). Your callback
/// inherits the RT invariant for as long as it can run there.
template <typename T>
void OutputPort<T>::setOutputCallback(OutputCallback callback) {
    requireNoTrafficYet("setOutputCallback");
    outputCallback_ = std::move(callback);
}

template <typename T>
void OutputPort<T>::reset() {
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        currentData_ = T{};
    } else {
        currentData_ = T{};
    }
    hasNanWarned_ = false;
}

// AsyncInputPort template implementations
template <typename T>
AsyncInputPort<T>::AsyncInputPort(int id, const std::string& name, ProcessingUnit* owner,
                                  bool threadSafe)
    : BasePort(id, name, PortType::ASYNC_INPUT, owner), isThreadSafe_(threadSafe) {
    data_ = T{};
}

template <typename T>
void AsyncInputPort<T>::update(const T& data) {
    incrementTotalMessages();

    // Validate data if callback is set
    if (validationCallback_ && !validationCallback_(data)) {
        incrementInvalidMessages();
        return;
    }

    // Check for NaN in floating point data
    if (!validateData(data)) {
        incrementInvalidMessages();
        return;
    }

    incrementValidMessages();

    // Store data. V11: threadSafe=true's dataMutex_ is the C31 sanctioned
    // exception — see InputPort<T>::read()'s comment.
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = data;
    } else {
        data_ = data;
    }

    wasUpdated_.store(true);

    // Call data callback if set. V13: runs inline on update()'s caller's
    // thread — inherits the RT invariant there, same contract as
    // InputPort<T>::writeData's dataCallback_.
    if (dataCallback_) {
        dataCallback_(data);
    }

    // Propagate to bridged ports: snapshot under bridgeMutex_, dispatch outside
    // it (V9/C35) — see InputPort<T>::writeData's identical comment.
    BridgeSnapshot bridged = snapshotBridgedPorts();
    if (bridged) {
        for (auto* bridgedPort : *bridged) {
            if (bridgedPort) {
                bridgedPort->update(data);
            }
        }
    }
}

template <typename T>
bool AsyncInputPort<T>::wasUpdated() const noexcept {
    return wasUpdated_.load();
}

template <typename T>
void AsyncInputPort<T>::read(T& data) {
    if (!wasUpdated_.load()) {
        // V12: preformatted into a stack buffer instead of std::stringstream —
        // this branch is reachable once per tick in a miswired system and
        // should not pay for iostream formatting machinery. std::runtime_error
        // still copies the message into its own (likely heap-backed) string,
        // so this isn't zero-alloc; a fully allocation-free error would need a
        // dedicated exception type, which would break existing
        // EXPECT_THROW(..., std::runtime_error) call sites, and the audit table
        // marks this item low-priority.
        char message[96];
        std::snprintf(message, sizeof(message),
                      "Error: Async Port %d was read without being updated", getId());
        throw std::runtime_error(message);
    }

    wasUpdated_.store(false);

    // V11: C31 sanctioned exception, same as the store above.
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data = data_;
    } else {
        data = data_;
    }
}

template <typename T>
T AsyncInputPort<T>::read() {
    T data;
    read(data);
    return data;
}

/// V13: runs on the caller-of-update()'s thread, inheriting the RT invariant
/// there — same contract as InputPort<T>::setValidationCallback.
template <typename T>
void AsyncInputPort<T>::setValidationCallback(ValidationCallback callback) {
    requireNoTrafficYet("setValidationCallback");
    validationCallback_ = std::move(callback);
}

/// V13: same contract as setValidationCallback above.
template <typename T>
void AsyncInputPort<T>::setDataCallback(DataCallback callback) {
    requireNoTrafficYet("setDataCallback");
    dataCallback_ = std::move(callback);
}

template <typename T>
bool AsyncInputPort<T>::isBridgePort() const noexcept {
    BridgeSnapshot bridged = snapshotBridgedPorts();
    return bridged && !bridged->empty();
}

template <typename T>
void AsyncInputPort<T>::addBridgedPort(AsyncInputPort<T>* bridgedPort) {
    if (!bridgedPort)
        return;

    std::lock_guard<std::mutex> lock(bridgeMutex_);
    BridgeList updated = copyBridgedForUpdateLocked();
    if (std::find(updated.begin(), updated.end(), bridgedPort) == updated.end()) {
        updated.push_back(bridgedPort);
        bridgedPorts_ = std::make_shared<const BridgeList>(std::move(updated));
    }
}

template <typename T>
void AsyncInputPort<T>::removeBridgedPort(AsyncInputPort<T>* bridgedPort) {
    std::lock_guard<std::mutex> lock(bridgeMutex_);
    BridgeList updated = copyBridgedForUpdateLocked();
    auto it = std::find(updated.begin(), updated.end(), bridgedPort);
    if (it != updated.end()) {
        updated.erase(it);
        bridgedPorts_ = std::make_shared<const BridgeList>(std::move(updated));
    }
}

template <typename T>
void AsyncInputPort<T>::reset() {
    if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = T{};
    } else {
        data_ = T{};
    }
    wasUpdated_.store(false);
}

// AsyncOutputPort template implementations
template <typename T>
AsyncOutputPort<T>::AsyncOutputPort(int id, const std::string& name, ProcessingUnit* owner,
                                    bool threadSafe)
    : BasePort(id, name, PortType::ASYNC_OUTPUT, owner), isThreadSafe_(threadSafe) {}

template <typename T>
void AsyncOutputPort<T>::write(const T& data) {
    incrementTotalMessages();

    // One snapshot serves both the emptiness check and the dispatch, so they
    // always agree, and neither runs under connectionMutex_ (C30, C35).
    ConnectionSnapshot targets = snapshotConnections();
    if (!targets || targets->empty()) {
        // Log warning about unconnected port
        return;
    }

    // Call output callback if set. V13: runs inline on write()'s caller's
    // thread — inherits the RT invariant there, same contract as
    // OutputPort<T>::write's outputCallback_.
    if (outputCallback_) {
        outputCallback_(data);
    }

    // Send to all connected async input ports
    for (auto* inputPort : *targets) {
        if (inputPort) {
            inputPort->update(data);
        }
    }
}

template <typename T>
void AsyncOutputPort<T>::connect(AsyncInputPort<T>* inputPort) {
    if (!inputPort)
        return;

    std::lock_guard<std::mutex> lock(connectionMutex_);
    ConnectionList updated = copyForUpdateLocked();
    if (std::find(updated.begin(), updated.end(), inputPort) == updated.end()) {
        updated.push_back(inputPort);
        connections_ = std::make_shared<const ConnectionList>(std::move(updated));
    }
}

template <typename T>
void AsyncOutputPort<T>::disconnect(AsyncInputPort<T>* inputPort) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    ConnectionList updated = copyForUpdateLocked();
    auto it = std::find(updated.begin(), updated.end(), inputPort);
    if (it != updated.end()) {
        updated.erase(it);
        connections_ = std::make_shared<const ConnectionList>(std::move(updated));
    }
}

template <typename T>
void AsyncOutputPort<T>::disconnectAll() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connections_.reset();
}

template <typename T>
bool AsyncOutputPort<T>::tryConnect(BasePort* target) {
    auto* typed = dynamic_cast<AsyncInputPort<T>*>(target);
    if (!typed) {
        return false;
    }
    connect(typed);
    return true;
}

template <typename T>
bool AsyncOutputPort<T>::tryDisconnect(BasePort* target) {
    auto* typed = dynamic_cast<AsyncInputPort<T>*>(target);
    if (!typed) {
        return false;
    }
    disconnect(typed);
    return true;
}

template <typename T>
bool AsyncOutputPort<T>::isConnected() const noexcept {
    ConnectionSnapshot targets = snapshotConnections();
    return targets && !targets->empty();
}

template <typename T>
size_t AsyncOutputPort<T>::getConnectionCount() const noexcept {
    ConnectionSnapshot targets = snapshotConnections();
    return targets ? targets->size() : 0;
}

template <typename T>
std::vector<AsyncInputPort<T>*> AsyncOutputPort<T>::getConnectedPorts() const {
    ConnectionSnapshot targets = snapshotConnections();
    return targets ? *targets : std::vector<AsyncInputPort<T>*>();
}

/// V13: runs on the caller-of-write()'s thread — inherits the RT invariant
/// there, same contract as OutputPort<T>::setOutputCallback.
template <typename T>
void AsyncOutputPort<T>::setOutputCallback(OutputCallback callback) {
    requireNoTrafficYet("setOutputCallback");
    outputCallback_ = std::move(callback);
}

template <typename T>
void AsyncOutputPort<T>::reset() {
    // Async output ports don't maintain state
}

} // namespace axonvex::core
