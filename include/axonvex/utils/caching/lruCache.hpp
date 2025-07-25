/**
 * @file lruCache.hpp
 * @brief LRU cache implementation
 */

#pragma once

#include <unordered_map>
#include <list>
#include <cstddef>

namespace axonvex::utils::caching {

template<typename K, typename V>
class LRUCache {
private:
    std::size_t capacity_;
    std::list<std::pair<K, V>> items_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> cache_map_;

public:
    explicit LRUCache(std::size_t capacity);
    
    bool get(const K& key, V& value);
    void put(const K& key, const V& value);
    void clear();
    std::size_t size() const;
    bool empty() const;
};

} // namespace axonvex::utils::caching