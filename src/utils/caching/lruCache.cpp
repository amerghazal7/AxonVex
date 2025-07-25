/**
 * @file lruCache.cpp
 * @brief LRU cache implementation
 */

#include <axonvex/utils/caching/lruCache.hpp>
#include <string>

namespace axonvex::utils::caching {

template<typename K, typename V>
LRUCache<K, V>::LRUCache(std::size_t capacity) : capacity_(capacity) {}

template<typename K, typename V>
bool LRUCache<K, V>::get(const K& key, V& value) {
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        return false;
    }
    
    // Move to front
    items_.splice(items_.begin(), items_, it->second);
    value = it->second->second;
    return true;
}

template<typename K, typename V>
void LRUCache<K, V>::put(const K& key, const V& value) {
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // Update existing item
        it->second->second = value;
        items_.splice(items_.begin(), items_, it->second);
        return;
    }
    
    // Add new item
    if (items_.size() >= capacity_) {
        // Remove least recently used
        auto last = items_.back();
        cache_map_.erase(last.first);
        items_.pop_back();
    }
    
    items_.emplace_front(key, value);
    cache_map_[key] = items_.begin();
}

template<typename K, typename V>
void LRUCache<K, V>::clear() {
    items_.clear();
    cache_map_.clear();
}

template<typename K, typename V>
std::size_t LRUCache<K, V>::size() const {
    return items_.size();
}

template<typename K, typename V>
bool LRUCache<K, V>::empty() const {
    return items_.empty();
}

// Explicit template instantiations
template class LRUCache<std::string, int>;
template class LRUCache<std::string, double>;
template class LRUCache<int, std::string>;

} // namespace axonvex::utils::caching