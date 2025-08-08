# Upgrading to AxonVex Containers v2

As of this release, container implementations were consolidated under `axonvex::utils::containers`.

What changed:
- Moved: `CircularBuffer` → `RingBuffer` in `axonvex::utils::containers`.
- Moved: `MemoryPool` and `ThreadSafeQueue` to `axonvex::utils::containers`.
- Removed: Core shim headers `include/axonvex/core/{circularBuffer,memoryPool,threadSafeQueue}.hpp`.

How to update your code:
- Replace includes:
  - `#include <axonvex/core/circularBuffer.hpp>` → `#include <axonvex/utils/containers/ringBuffer.hpp>`
  - `#include <axonvex/core/memoryPool.hpp>` → `#include <axonvex/utils/containers/memoryPool.hpp>`
  - `#include <axonvex/core/threadSafeQueue.hpp>` → `#include <axonvex/utils/containers/threadSafeQueue.hpp>`
- Update types:
  - `axonvex::core::CircularBuffer<T>` → `axonvex::utils::containers::RingBuffer<T>`
  - `axonvex::core::MemoryPool<T>` → `axonvex::utils::containers::MemoryPool<T>`
  - `axonvex::core::ThreadSafeQueue<T>` → `axonvex::utils::containers::ThreadSafeQueue<T>`

In core headers, convenience aliases bring these into the `axonvex::core` namespace:

```cpp
using axonvex::utils::containers::MemoryPool;
using axonvex::utils::containers::ThreadSafeQueue;
```

Examples and tests have been updated accordingly.

# Upgrading Guide

This guide documents breaking changes introduced in Phase 3 (interfaces/plugins refactor).

## 1. ProtocolInterface callbacks (BREAKING)

- Before:
  - Message callback: `std::function<void(const std::vector<uint8_t>&)>`
  - Set via: `setMessageCallback(std::function<...>)`
- Now:
  - Message callback type is `axonvex::core::Callback<std::vector<uint8_t>>`.
  - `ProtocolInterface` inherits `axonvex::core::CallerKeyed<std::string, std::vector<uint8_t>>` to support keyed subscriptions.
  - API:
    - `setMessageCallback(MessageCallback* cb)` registers the handler under key "default".
    - `registerMessageHandler(const std::string& key, MessageCallback* cb)`
    - `unregisterMessageHandler(const std::string& key, MessageCallback* cb)`
    - `unregisterAllMessageHandlersForKey(const std::string& key)`

Migration example:

```cpp
// Old
// proto->setMessageCallback([&](const std::vector<uint8_t>& data) { /* handle */ });

// New
struct MyHandler : public axonvex::core::Callback<std::vector<uint8_t>> {
    void callbackPerform(const std::vector<uint8_t> data) override {
        // handle
    }
};
MyHandler handler;
proto->setMessageCallback(&handler); // default channel
// or keyed
proto->registerMessageHandler("telemetry", &handler);
```

## 2. Error handling

- Convenience API remains: `setErrorCallback(std::function<void(const std::string&)>)`.
- New keyed API using `axonvex::core::Callback<std::string>`:
  - `registerErrorHandler(const std::string& key, ErrorHandler* cb)`
  - `unregisterErrorHandler(const std::string& key, ErrorHandler* cb)`
  - `unregisterAllErrorHandlersForKey(const std::string& key)`

## 3. Containers location (Phase 3A reminder)

- Legacy headers in `include/axonvex/core/{circularBuffer,memoryPool,threadSafeQueue}.hpp` have been removed.
- Use new locations under `include/axonvex/utils/containers/` and the new names (e.g., `ringBuffer.hpp`).

See README for more details.
