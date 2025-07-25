/**
 * @file concurrentMap.hpp
 * @brief Thread-safe concurrent map implementation
 */

#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <functional>

namespace axonvex::utils::containers {

template<typename K, typename V>
class ConcurrentMap {
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<K, V> map_;

public:
    void insert(const K& key, const V& value);
    bool find(const K& key, V& value) const;
    bool erase(const K& key);
    void clear();
    size_t size() const;
    bool empty() const;
    
    // Apply function to all elements (read-only)
    void forEach(std::function<void(const K&, const V&)> func) const;
};

} // namespace axonvex::utils::containers