#pragma once

#include <axonvex/types/primitives/primitives.hpp>
#include <vector>
#include <queue>
#include <bitset>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>
#include <type_traits>  // For std::is_invocable_v

namespace axonvex::types::collections {

    // High-performance ring buffer with statistics
    template<typename T>
    class RingBuffer {
    private:
        std::vector<T> buffer_;
        std::atomic<size_t> head_{0};
        std::atomic<size_t> tail_{0};
        std::atomic<size_t> size_{0};
        const size_t capacity_;
        
        // Statistics
        mutable std::atomic<uint64_t> total_writes_{0};
        mutable std::atomic<uint64_t> total_reads_{0};
        mutable std::atomic<uint64_t> overruns_{0};
        mutable std::atomic<uint64_t> underruns_{0};
        
    public:
        explicit RingBuffer(size_t capacity) 
            : buffer_(capacity), capacity_(capacity) {}
            
        // Non-copyable but movable
        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;
        RingBuffer(RingBuffer&&) = default;
        RingBuffer& operator=(RingBuffer&&) = default;
        
        // Write operations
        bool write(const T& item) {
            return write_impl(item);
        }
        
        bool write(T&& item) {
            return write_impl(std::move(item));
        }
        
        // Force write (overwrites oldest if full)
        void forceWrite(const T& item) {
            force_write_impl(item);
        }
        
        void forceWrite(T&& item) {
            force_write_impl(std::move(item));
        }
        
        // Read operations
        bool read(T& item) {
            if (size_.load() == 0) {
                underruns_.fetch_add(1);
                return false;
            }
            
            size_t current_tail = tail_.load();
            item = std::move(buffer_[current_tail]);
            tail_.store((current_tail + 1) % capacity_);
            size_.fetch_sub(1);
            total_reads_.fetch_add(1);
            
            return true;
        }
        
        // Peek without removing
        bool peek(T& item) const {
            if (size_.load() == 0) {
                return false;
            }
            
            item = buffer_[tail_.load()];
            return true;
        }
        
        // Capacity and size
        size_t capacity() const noexcept { return capacity_; }
        size_t size() const noexcept { return size_.load(); }
        bool empty() const noexcept { return size_.load() == 0; }
        bool full() const noexcept { return size_.load() == capacity_; }
        
        // Statistics
        uint64_t totalWrites() const noexcept { return total_writes_.load(); }
        uint64_t totalReads() const noexcept { return total_reads_.load(); }
        uint64_t overruns() const noexcept { return overruns_.load(); }
        uint64_t underruns() const noexcept { return underruns_.load(); }
        
        double utilizationRatio() const noexcept {
            return static_cast<double>(size_.load()) / capacity_;
        }
        
        void resetStatistics() {
            total_writes_.store(0);
            total_reads_.store(0);
            overruns_.store(0);
            underruns_.store(0);
        }
        
        // Clear all data
        void clear() {
            head_.store(0);
            tail_.store(0);
            size_.store(0);
        }
        
    private:
        template<typename U>
        bool write_impl(U&& item) {
            if (size_.load() >= capacity_) {
                overruns_.fetch_add(1);
                return false;
            }
            
            size_t current_head = head_.load();
            buffer_[current_head] = std::forward<U>(item);
            head_.store((current_head + 1) % capacity_);
            size_.fetch_add(1);
            total_writes_.fetch_add(1);
            
            return true;
        }
        
        template<typename U>
        void force_write_impl(U&& item) {
            size_t current_head = head_.load();
            buffer_[current_head] = std::forward<U>(item);
            
            if (size_.load() >= capacity_) {
                // Buffer is full, advance tail (overwrite oldest)
                tail_.store((tail_.load() + 1) % capacity_);
                overruns_.fetch_add(1);
            } else {
                size_.fetch_add(1);
            }
            
            head_.store((current_head + 1) % capacity_);
            total_writes_.fetch_add(1);
        }
    };

    // Thread-safe priority queue
    template<typename T, typename Compare = std::less<T>>
    class PriorityQueue {
    private:
        std::priority_queue<T, std::vector<T>, Compare> queue_;
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        bool shutdown_{false};
        
    public:
        PriorityQueue() = default;
        
        // Non-copyable but movable
        PriorityQueue(const PriorityQueue&) = delete;
        PriorityQueue& operator=(const PriorityQueue&) = delete;
        PriorityQueue(PriorityQueue&&) = default;
        PriorityQueue& operator=(PriorityQueue&&) = default;
        
        ~PriorityQueue() {
            shutdown();
        }
        
        // Push operations
        void push(const T& item) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!shutdown_) {
                queue_.push(item);
                condition_.notify_one();
            }
        }
        
        void push(T&& item) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!shutdown_) {
                queue_.push(std::move(item));
                condition_.notify_one();
            }
        }
        
        template<typename... Args>
        void emplace(Args&&... args) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!shutdown_) {
                queue_.emplace(std::forward<Args>(args)...);
                condition_.notify_one();
            }
        }
        
        // Pop operations
        bool pop(T& item) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty() || shutdown_) {
                return false;
            }
            
            item = std::move(const_cast<T&>(queue_.top()));
            queue_.pop();
            return true;
        }
        
        // Blocking pop with timeout
        template<typename Rep, typename Period>
        bool popWaitFor(T& item, const std::chrono::duration<Rep, Period>& timeout) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (condition_.wait_for(lock, timeout, [this] { 
                return !queue_.empty() || shutdown_; 
            })) {
                if (!queue_.empty() && !shutdown_) {
                    item = std::move(const_cast<T&>(queue_.top()));
                    queue_.pop();
                    return true;
                }
            }
            
            return false;
        }
        
        // Peek operations
        bool peek(T& item) const {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty() || shutdown_) {
                return false;
            }
            
            item = queue_.top();
            return true;
        }
        
        // Size operations
        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }
        
        bool empty() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }
        
        // Control operations
        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!queue_.empty()) {
                queue_.pop();
            }
        }
        
        void shutdown() {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
            condition_.notify_all();
        }
        
        bool isShutdown() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return shutdown_;
        }
    };

    // Efficient bit set operations with enhanced functionality
    class BitSet {
    private:
        std::vector<uint64_t> bits_;
        size_t size_;
        
        static constexpr size_t BITS_PER_WORD = 64;
        
    public:
        explicit BitSet(size_t size = 0) : size_(size) {
            size_t words = (size + BITS_PER_WORD - 1) / BITS_PER_WORD;
            bits_.resize(words, 0);
        }
        
        // Copy and move constructors
        BitSet(const BitSet&) = default;
        BitSet& operator=(const BitSet&) = default;
        BitSet(BitSet&&) = default;
        BitSet& operator=(BitSet&&) = default;
        
        // Bit operations
        void set(size_t pos, bool value = true) {
            if (pos >= size_) {
                resize(pos + 1);
            }
            
            size_t word_index = pos / BITS_PER_WORD;
            size_t bit_index = pos % BITS_PER_WORD;
            
            if (value) {
                bits_[word_index] |= (1ULL << bit_index);
            } else {
                bits_[word_index] &= ~(1ULL << bit_index);
            }
        }
        
        bool get(size_t pos) const {
            if (pos >= size_) {
                return false;
            }
            
            size_t word_index = pos / BITS_PER_WORD;
            size_t bit_index = pos % BITS_PER_WORD;
            
            return (bits_[word_index] & (1ULL << bit_index)) != 0;
        }
        
        void flip(size_t pos) {
            set(pos, !get(pos));
        }
        
        void reset(size_t pos) {
            set(pos, false);
        }
        
        // Bulk operations
        void setAll() {
            std::fill(bits_.begin(), bits_.end(), ~0ULL);
            // Clear any extra bits in the last word
            if (size_ % BITS_PER_WORD != 0) {
                size_t last_word = bits_.size() - 1;
                size_t valid_bits = size_ % BITS_PER_WORD;
                uint64_t mask = (1ULL << valid_bits) - 1;
                bits_[last_word] &= mask;
            }
        }
        
        void clearAll() {
            std::fill(bits_.begin(), bits_.end(), 0);
        }
        
        void flipAll() {
            for (auto& word : bits_) {
                word = ~word;
            }
            // Clear any extra bits in the last word
            if (size_ % BITS_PER_WORD != 0) {
                size_t last_word = bits_.size() - 1;
                size_t valid_bits = size_ % BITS_PER_WORD;
                uint64_t mask = (1ULL << valid_bits) - 1;
                bits_[last_word] &= mask;
            }
        }
        
        // Logical operations
        BitSet operator&(const BitSet& other) const {
            BitSet result(std::max(size_, other.size_));
            size_t min_words = std::min(bits_.size(), other.bits_.size());
            
            for (size_t i = 0; i < min_words; ++i) {
                result.bits_[i] = bits_[i] & other.bits_[i];
            }
            
            return result;
        }
        
        BitSet operator|(const BitSet& other) const {
            BitSet result(std::max(size_, other.size_));
            size_t max_words = std::max(bits_.size(), other.bits_.size());
            
            for (size_t i = 0; i < max_words; ++i) {
                uint64_t a = (i < bits_.size()) ? bits_[i] : 0;
                uint64_t b = (i < other.bits_.size()) ? other.bits_[i] : 0;
                result.bits_[i] = a | b;
            }
            
            return result;
        }
        
        BitSet operator^(const BitSet& other) const {
            BitSet result(std::max(size_, other.size_));
            size_t max_words = std::max(bits_.size(), other.bits_.size());
            
            for (size_t i = 0; i < max_words; ++i) {
                uint64_t a = (i < bits_.size()) ? bits_[i] : 0;
                uint64_t b = (i < other.bits_.size()) ? other.bits_[i] : 0;
                result.bits_[i] = a ^ b;
            }
            
            return result;
        }
        
        BitSet operator~() const {
            BitSet result(*this);
            result.flipAll();
            return result;
        }
        
        // Assignment operators
        BitSet& operator&=(const BitSet& other) {
            *this = *this & other;
            return *this;
        }
        
        BitSet& operator|=(const BitSet& other) {
            *this = *this | other;
            return *this;
        }
        
        BitSet& operator^=(const BitSet& other) {
            *this = *this ^ other;
            return *this;
        }
        
        // Comparison operators
        bool operator==(const BitSet& other) const {
            if (size_ != other.size_) {
                return false;
            }
            return bits_ == other.bits_;
        }
        
        bool operator!=(const BitSet& other) const {
            return !(*this == other);
        }
        
        // Information
        size_t size() const noexcept { return size_; }
        
        size_t count() const {
            size_t total = 0;
            for (const auto& word : bits_) {
                total += __builtin_popcountll(word);
            }
            return total;
        }
        
        bool any() const {
            for (const auto& word : bits_) {
                if (word != 0) return true;
            }
            return false;
        }
        
        bool none() const {
            return !any();
        }
        
        bool all() const {
            return count() == size_;
        }
        
        // Find operations
        size_t findFirst() const {
            for (size_t i = 0; i < bits_.size(); ++i) {
                if (bits_[i] != 0) {
                    return i * BITS_PER_WORD + __builtin_ctzll(bits_[i]);
                }
            }
            return size_;  // Not found
        }
        
        size_t findNext(size_t pos) const {
            if (pos >= size_) return size_;
            
            size_t word_index = pos / BITS_PER_WORD;
            size_t bit_index = pos % BITS_PER_WORD;
            
            // Check remaining bits in current word
            uint64_t word = bits_[word_index] & (~0ULL << bit_index);
            if (word != 0) {
                return word_index * BITS_PER_WORD + __builtin_ctzll(word);
            }
            
            // Check subsequent words
            for (size_t i = word_index + 1; i < bits_.size(); ++i) {
                if (bits_[i] != 0) {
                    return i * BITS_PER_WORD + __builtin_ctzll(bits_[i]);
                }
            }
            
            return size_;  // Not found
        }
        
        // Resize
        void resize(size_t new_size) {
            size_t old_size = size_;
            size_ = new_size;
            
            size_t new_words = (new_size + BITS_PER_WORD - 1) / BITS_PER_WORD;
            bits_.resize(new_words, 0);
            
            // Clear any invalid bits in the last word
            if (new_size < old_size && new_size % BITS_PER_WORD != 0) {
                size_t last_word = bits_.size() - 1;
                size_t valid_bits = new_size % BITS_PER_WORD;
                uint64_t mask = (1ULL << valid_bits) - 1;
                bits_[last_word] &= mask;
            }
        }
        
        // Iterator support for set bits
        class SetBitIterator {
        private:
            const BitSet* bitset_;
            size_t pos_;
            
        public:
            SetBitIterator(const BitSet* bitset, size_t pos) 
                : bitset_(bitset), pos_(pos) {}
                
            size_t operator*() const { return pos_; }
            
            SetBitIterator& operator++() {
                pos_ = bitset_->findNext(pos_ + 1);
                return *this;
            }
            
            bool operator!=(const SetBitIterator& other) const {
                return pos_ != other.pos_;
            }
            
            bool operator==(const SetBitIterator& other) const {
                return pos_ == other.pos_;
            }
        };
        
        SetBitIterator begin() const {
            return SetBitIterator(this, findFirst());
        }
        
        SetBitIterator end() const {
            return SetBitIterator(this, size_);
        }
    };

    // Object pool for efficient memory management
    template<typename T>
    class ObjectPool {
    private:
        std::vector<std::unique_ptr<T>> pool_;
        std::vector<T*> available_;
        mutable std::mutex mutex_;  // Make mutex mutable for const methods
        std::function<std::unique_ptr<T>()> factory_;
        size_t max_size_;
        
    public:
        // Default constructor with standard factory
        explicit ObjectPool(size_t initial_size = 10, size_t max_size = 1000)
            : max_size_(max_size), factory_([](){ return std::make_unique<T>(); }) {
            pool_.reserve(max_size_);
            available_.reserve(max_size_);
            
            for (size_t i = 0; i < initial_size; ++i) {
                auto obj = factory_();
                available_.push_back(obj.get());
                pool_.push_back(std::move(obj));
            }
        }
        
        // Constructor with custom factory - explicit template to avoid ambiguity
        template<typename Factory, 
                 typename = std::enable_if_t<std::is_invocable_v<Factory> && 
                                           !std::is_arithmetic_v<std::decay_t<Factory>>>>
        explicit ObjectPool(Factory&& factory, size_t initial_size = 10, size_t max_size = 1000)
            : max_size_(max_size), factory_(std::forward<Factory>(factory)) {
            pool_.reserve(max_size_);
            available_.reserve(max_size_);
            
            for (size_t i = 0; i < initial_size; ++i) {
                auto obj = factory_();
                available_.push_back(obj.get());
                pool_.push_back(std::move(obj));
            }
        }
        
        // Acquire an object from the pool
        std::unique_ptr<T, std::function<void(T*)>> acquire() {
            std::lock_guard<std::mutex> lock(mutex_);
            
            T* obj = nullptr;
            
            if (!available_.empty()) {
                obj = available_.back();
                available_.pop_back();
            } else if (pool_.size() < max_size_) {
                auto new_obj = factory_();
                obj = new_obj.get();
                pool_.push_back(std::move(new_obj));
            }
            
            if (obj) {
                return std::unique_ptr<T, std::function<void(T*)>>(
                    obj, [this](T* ptr) { this->release(ptr); });
            }
            
            // Pool exhausted, create temporary object
            return std::unique_ptr<T, std::function<void(T*)>>(
                factory_().release(), [](T* ptr) { delete ptr; });
        }
        
        // Pool statistics
        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return pool_.size();
        }
        
        size_t available() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return available_.size();
        }
        
        size_t inUse() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return pool_.size() - available_.size();
        }
        
        double utilizationRatio() const {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pool_.empty()) return 0.0;
            return static_cast<double>(pool_.size() - available_.size()) / pool_.size();
        }
        
    private:
        void release(T* obj) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Check if this object belongs to our pool
            auto it = std::find_if(pool_.begin(), pool_.end(),
                [obj](const std::unique_ptr<T>& ptr) { return ptr.get() == obj; });
                
            if (it != pool_.end()) {
                available_.push_back(obj);
            }
            // If not from our pool, it will be deleted by the custom deleter
        }
    };

} // namespace axonvex::types::collections