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
