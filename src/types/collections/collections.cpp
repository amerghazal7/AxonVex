#include <axonvex/types/collections/collections.hpp>

namespace axonvex::types::collections {

// Implementation file for collections module
// Most functionality is template-based and implemented in the header

/**
 * @brief Initialize collections subsystem
 */
void initialize() {
    // Initialize any global collection resources if needed
}

/**
 * @brief Cleanup collections subsystem
 */
void cleanup() {
    // Cleanup any global collection resources if needed
}

} // namespace axonvex::types::collections

// Explicit template instantiations for common types to reduce compile time
// These are placed outside the namespace to avoid cross-namespace instantiation issues

// CircularBuffer instantiations (unified implementation)
template class axonvex::core::CircularBuffer<int>;
template class axonvex::core::CircularBuffer<double>;
template class axonvex::core::CircularBuffer<float>;
template class axonvex::core::CircularBuffer<char>;

// MemoryPool instantiations (unified implementation)
template class axonvex::core::MemoryPool<int>;
template class axonvex::core::MemoryPool<double>;
template class axonvex::core::MemoryPool<float>;

// ThreadSafeQueue instantiations (unified implementation)
template class axonvex::core::ThreadSafeQueue<int>;
template class axonvex::core::ThreadSafeQueue<double>;
template class axonvex::core::ThreadSafeQueue<float>;
template class axonvex::core::ThreadSafeQueue<std::string>;

// Note: Legacy collections now use these unified implementations through aliases
// No need for separate template instantiations since the aliases redirect to the above