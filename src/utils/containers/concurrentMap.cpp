/**
 * @file concurrentMap.cpp
 * @brief Concurrent map implementation
 */

#include <axonvex/utils/containers/concurrentMap.hpp>
#include <mutex>

namespace axonvex::utils::containers {

template<typename K, typename V>
void ConcurrentMap<K, V>::insert(const K& key, const V& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_[key] = value;
}

template<typename K, typename V>
bool ConcurrentMap<K, V>::find(const K& key, V& value) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

template<typename K, typename V>
bool ConcurrentMap<K, V>::erase(const K& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return map_.erase(key) > 0;
}

template<typename K, typename V>
void ConcurrentMap<K, V>::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_.clear();
}

template<typename K, typename V>
size_t ConcurrentMap<K, V>::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.size();
}

template<typename K, typename V>
bool ConcurrentMap<K, V>::empty() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.empty();
}

template<typename K, typename V>
void ConcurrentMap<K, V>::forEach(std::function<void(const K&, const V&)> func) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& pair : map_) {
        func(pair.first, pair.second);
    }
}

// Explicit template instantiations
template class ConcurrentMap<std::string, int>;
template class ConcurrentMap<std::string, double>;
template class ConcurrentMap<int, std::string>;

} // namespace axonvex::utils::containers