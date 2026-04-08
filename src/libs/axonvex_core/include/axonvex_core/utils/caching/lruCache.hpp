#pragma once

#include <axonvex_core/utils/optional.hpp>
#include <list>
#include <mutex>
#include <unordered_map>

namespace axonvex::utils::caching {

template <typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = value;
            list_.splice(list_.begin(), list_, it->second);
            return;
        }
        if (list_.size() >= capacity_) {
            auto last = list_.back();
            map_.erase(last.first);
            list_.pop_back();
        }
        list_.emplace_front(key, value);
        map_[key] = list_.begin();
    }

    axonvex::optional<V> get(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return axonvex::nullopt;
        list_.splice(list_.begin(), list_, it->second);
        return it->second->second;
    }

    bool contains(const K& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.find(key) != map_.end();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return list_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        list_.clear();
        map_.clear();
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::list<std::pair<K, V>> list_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> map_;
};

} // namespace axonvex::utils::caching
