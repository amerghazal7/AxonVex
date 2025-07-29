#pragma once

#include <axonvex/core/memoryPool.hpp>
#include <axonvex/core/circularBuffer.hpp>
#include <axonvex/core/threadSafeQueue.hpp>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace axonvex::types::collections {

/**
 * @brief Type alias for core CircularBuffer - eliminates RingBuffer redundancy
 * @tparam T Element type
 */
template<typename T>
using RingBuffer = axonvex::core::CircularBuffer<T>;

/**
 * @brief Type alias for core ThreadSafeQueue - provides queue functionality
 * @tparam T Element type  
 */
template<typename T>
using LockFreeQueue = axonvex::core::ThreadSafeQueue<T>;

/**
 * @brief Type alias for core MemoryPool - eliminates ObjectPool redundancy
 * @tparam T Element type
 */
template<typename T>
using ObjectPool = axonvex::core::MemoryPool<T>;

/**
 * @brief Thread-safe vector using core MemoryPool for allocation
 * @tparam T Element type
 */
template<typename T>
class ThreadSafeVector {
public:
    explicit ThreadSafeVector(size_t initialCapacity = 64, size_t poolSize = 1024)
        : pool_(poolSize), capacity_(initialCapacity), size_(0) {
        data_ = std::make_unique<std::atomic<T*>[]>(capacity_);
        for (size_t i = 0; i < capacity_; ++i) {
            data_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    bool push_back(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ >= capacity_) {
            if (!resize(capacity_ * 2)) {
                return false;
            }
        }
        
        T* pooled_item = pool_.allocateObject(item);
        if (!pooled_item) {
            return false;
        }
        
        data_[size_].store(pooled_item, std::memory_order_release);
        ++size_;
        return true;
    }

    bool pop_back(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0) {
            return false;
        }
        
        --size_;
        T* pooled_item = data_[size_].load(std::memory_order_acquire);
        if (pooled_item) {
            item = *pooled_item;
            pool_.deallocateObject(pooled_item);
            data_[size_].store(nullptr, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    bool at(size_t index, T& item) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= size_) {
            return false;
        }
        
        T* pooled_item = data_[index].load(std::memory_order_acquire);
        if (pooled_item) {
            item = *pooled_item;
            return true;
        }
        return false;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < size_; ++i) {
            T* pooled_item = data_[i].load(std::memory_order_relaxed);
            if (pooled_item) {
                pool_.deallocateObject(pooled_item);
                data_[i].store(nullptr, std::memory_order_relaxed);
            }
        }
        size_ = 0;
    }

private:
    mutable std::mutex mutex_;
    axonvex::core::MemoryPool<T> pool_;
    std::unique_ptr<std::atomic<T*>[]> data_;
    size_t capacity_;
    size_t size_;

    bool resize(size_t newCapacity) {
        auto newData = std::make_unique<std::atomic<T*>[]>(newCapacity);
        for (size_t i = 0; i < capacity_; ++i) {
            newData[i].store(data_[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        for (size_t i = capacity_; i < newCapacity; ++i) {
            newData[i].store(nullptr, std::memory_order_relaxed);
        }
        data_ = std::move(newData);
        capacity_ = newCapacity;
        return true;
    }
};

/**
 * @brief Lock-free stack implementation using core MemoryPool
 * @tparam T Element type
 */
template<typename T>
class LockFreeStack {
public:
    explicit LockFreeStack(size_t poolSize = 1024) : pool_(poolSize), head_(nullptr) {}
    
    ~LockFreeStack() {
        clear();
    }
    
    bool push(const T& data) {
        Node* new_node = pool_.allocateObject();
        if (!new_node) {
            return false; // Pool exhausted
        }
        
        // Use placement new to construct the data
        new(&new_node->data) T(data);
        new_node->next = head_.load(std::memory_order_relaxed);
        
        while (!head_.compare_exchange_weak(new_node->next, new_node, 
                                          std::memory_order_release, 
                                          std::memory_order_relaxed)) {
            // Retry until successful
        }
        return true;
    }
    
    bool pop(T& result) {
        Node* old_head = head_.load(std::memory_order_acquire);
        
        while (old_head && !head_.compare_exchange_weak(old_head, old_head->next,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed)) {
            // Retry until successful
        }
        
        if (old_head) {
            result = old_head->data;
            pool_.deallocateObject(old_head);
            return true;
        }
        return false;
    }
    
    bool empty() const {
        return head_.load(std::memory_order_acquire) == nullptr;
    }

    void clear() {
        Node* current = head_.load(std::memory_order_relaxed);
        head_.store(nullptr, std::memory_order_relaxed);
        
        while (current) {
            Node* next = current->next;
            pool_.deallocateObject(current);
            current = next;
        }
    }

private:
    struct Node {
        T data;
        Node* next;
        
        Node() : next(nullptr) {}
    };
    
    axonvex::core::MemoryPool<Node> pool_;
    std::atomic<Node*> head_;
};

/**
 * @brief High-performance priority queue with configurable comparison
 * @tparam T Element type
 * @tparam Compare Comparison function
 */
template<typename T, typename Compare = std::less<T>>
class PriorityQueue {
public:
    explicit PriorityQueue(size_t initialCapacity = 16) 
        : heap_(initialCapacity), size_(0), compare_() {}
    
    void push(const T& item) {
        if (size_ >= heap_.size()) {
            heap_.resize(heap_.size() * 2);
        }
        
        heap_[size_] = item;
        bubbleUp(size_);
        ++size_;
    }
    
    bool pop(T& item) {
        if (size_ == 0) return false;
        
        item = heap_[0];
        heap_[0] = heap_[size_ - 1];
        --size_;
        
        if (size_ > 0) {
            bubbleDown(0);
        }
        
        return true;
    }
    
    const T& top() const {
        return heap_[0];
    }
    
    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    
    void clear() { size_ = 0; }

private:
    std::vector<T> heap_;
    size_t size_;
    Compare compare_;
    
    void bubbleUp(size_t index) {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            if (!compare_(heap_[index], heap_[parent])) break;
            
            std::swap(heap_[index], heap_[parent]);
            index = parent;
        }
    }
    
    void bubbleDown(size_t index) {
        while (true) {
            size_t smallest = index;
            size_t left = 2 * index + 1;
            size_t right = 2 * index + 2;
            
            if (left < size_ && compare_(heap_[left], heap_[smallest])) {
                smallest = left;
            }
            
            if (right < size_ && compare_(heap_[right], heap_[smallest])) {
                smallest = right;
            }
            
            if (smallest == index) break;
            
            std::swap(heap_[index], heap_[smallest]);
            index = smallest;
        }
    }
};

/**
 * @brief Memory-efficient sparse array implementation
 * @tparam T Element type
 */
template<typename T>
class SparseArray {
public:
    SparseArray() = default;
    
    void set(size_t index, const T& value) {
        data_[index] = value;
    }
    
    bool get(size_t index, T& value) const {
        auto it = data_.find(index);
        if (it != data_.end()) {
            value = it->second;
            return true;
        }
        return false;
    }
    
    T get(size_t index, const T& defaultValue = T{}) const {
        auto it = data_.find(index);
        return (it != data_.end()) ? it->second : defaultValue;
    }
    
    bool has(size_t index) const {
        return data_.find(index) != data_.end();
    }
    
    void remove(size_t index) {
        data_.erase(index);
    }
    
    void clear() {
        data_.clear();
    }
    
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    
    // Iterator support
    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }
    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }

private:
    std::unordered_map<size_t, T> data_;
};

} // namespace axonvex::types::collections