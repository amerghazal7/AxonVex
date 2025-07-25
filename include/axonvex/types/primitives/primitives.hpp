#pragma once

#include <chrono>
#include <atomic>
#include <cstdint>
#include <string>
#include <memory>
#include <random>

namespace axonvex::types::primitives {

    // Real-time time types
    using RealTime = std::chrono::nanoseconds;
    using Frequency = double;  // Frequency in Hz
    using Duration = std::chrono::microseconds;
    using TimePoint = std::chrono::high_resolution_clock::time_point;

    // Enhanced precision types
    using Float32 = float;
    using Float64 = double;
    using Int8 = std::int8_t;
    using Int16 = std::int16_t;
    using Int32 = std::int32_t;
    using Int64 = std::int64_t;
    using UInt8 = std::uint8_t;
    using UInt16 = std::uint16_t;
    using UInt32 = std::uint32_t;
    using UInt64 = std::uint64_t;

    // Enhanced atomic types with performance optimizations
    template<typename T>
    class Atomic {
    private:
        alignas(64) std::atomic<T> value_;  // Cache line aligned
        
    public:
        Atomic() : value_(T{}) {}
        explicit Atomic(const T& value) : value_(value) {}
        
        // Copy constructor (deleted for atomic safety)
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;
        
        // Move constructor
        Atomic(Atomic&& other) noexcept : value_(other.value_.load()) {}
        
        // Basic operations
        T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
            return value_.load(order);
        }
        
        void store(const T& value, std::memory_order order = std::memory_order_seq_cst) noexcept {
            value_.store(value, order);
        }
        
        T exchange(const T& value, std::memory_order order = std::memory_order_seq_cst) noexcept {
            return value_.exchange(value, order);
        }
        
        bool compare_exchange_weak(T& expected, const T& desired,
                                 std::memory_order order = std::memory_order_seq_cst) noexcept {
            return value_.compare_exchange_weak(expected, desired, order);
        }
        
        bool compare_exchange_strong(T& expected, const T& desired,
                                   std::memory_order order = std::memory_order_seq_cst) noexcept {
            return value_.compare_exchange_strong(expected, desired, order);
        }
        
        // Arithmetic operations (for numeric types)
        template<typename U = T>
        typename std::enable_if_t<std::is_arithmetic_v<U>, T>
        fetch_add(const T& value, std::memory_order order = std::memory_order_seq_cst) noexcept {
            return value_.fetch_add(value, order);
        }
        
        template<typename U = T>
        typename std::enable_if_t<std::is_arithmetic_v<U>, T>
        fetch_sub(const T& value, std::memory_order order = std::memory_order_seq_cst) noexcept {
            return value_.fetch_sub(value, order);
        }
        
        // Operators
        operator T() const noexcept {
            return load();
        }
        
        T operator=(const T& value) noexcept {
            store(value);
            return value;
        }
        
        T operator++() noexcept {
            return fetch_add(1) + 1;
        }
        
        T operator++(int) noexcept {
            return fetch_add(1);
        }
        
        T operator--() noexcept {
            return fetch_sub(1) - 1;
        }
        
        T operator--(int) noexcept {
            return fetch_sub(1);
        }
    };

    // UUID class for unique identification
    class UUID {
    private:
        std::array<UInt8, 16> data_;
        
    public:
        UUID();
        explicit UUID(const std::string& str);
        UUID(const UUID&) = default;
        UUID& operator=(const UUID&) = default;
        UUID(UUID&&) = default;
        UUID& operator=(UUID&&) = default;
        
        // Generate new UUID
        static UUID generate();
        static UUID generateRandom();
        static UUID generateTimeBasedMark();
        
        // String conversion
        std::string toString() const;
        void fromString(const std::string& str);
        
        // Raw data access
        const std::array<UInt8, 16>& data() const noexcept { return data_; }
        
        // Comparison operators
        bool operator==(const UUID& other) const noexcept;
        bool operator!=(const UUID& other) const noexcept;
        bool operator<(const UUID& other) const noexcept;
        
        // Hash support
        std::size_t hash() const noexcept;
        
        // Null UUID
        static const UUID& null();
        bool isNull() const noexcept;
        
        // Version and variant
        int version() const noexcept;
        int variant() const noexcept;
    };

    // High-precision floating point operations
    class PrecisionFloat {
    private:
        double value_;
        double epsilon_;
        
    public:
        explicit PrecisionFloat(double value = 0.0, double epsilon = 1e-15)
            : value_(value), epsilon_(epsilon) {}
            
        // Value access
        double value() const noexcept { return value_; }
        double epsilon() const noexcept { return epsilon_; }
        
        void setValue(double value) noexcept { value_ = value; }
        void setEpsilon(double epsilon) noexcept { epsilon_ = epsilon; }
        
        // Comparison with epsilon
        bool equals(const PrecisionFloat& other) const noexcept;
        bool equals(double other) const noexcept;
        
        // Arithmetic operators
        PrecisionFloat operator+(const PrecisionFloat& other) const;
        PrecisionFloat operator-(const PrecisionFloat& other) const;
        PrecisionFloat operator*(const PrecisionFloat& other) const;
        PrecisionFloat operator/(const PrecisionFloat& other) const;
        
        PrecisionFloat& operator+=(const PrecisionFloat& other);
        PrecisionFloat& operator-=(const PrecisionFloat& other);
        PrecisionFloat& operator*=(const PrecisionFloat& other);
        PrecisionFloat& operator/=(const PrecisionFloat& other);
        
        // Comparison operators
        bool operator==(const PrecisionFloat& other) const noexcept;
        bool operator!=(const PrecisionFloat& other) const noexcept;
        bool operator<(const PrecisionFloat& other) const noexcept;
        bool operator<=(const PrecisionFloat& other) const noexcept;
        bool operator>(const PrecisionFloat& other) const noexcept;
        bool operator>=(const PrecisionFloat& other) const noexcept;
        
        // Conversion
        operator double() const noexcept { return value_; }
    };

    // Type-safe ID template
    template<typename Tag>
    class TypedID {
    private:
        UInt64 id_;
        
    public:
        explicit TypedID(UInt64 id = 0) : id_(id) {}
        
        UInt64 value() const noexcept { return id_; }
        
        bool operator==(const TypedID& other) const noexcept {
            return id_ == other.id_;
        }
        
        bool operator!=(const TypedID& other) const noexcept {
            return id_ != other.id_;
        }
        
        bool operator<(const TypedID& other) const noexcept {
            return id_ < other.id_;
        }
        
        std::size_t hash() const noexcept {
            return std::hash<UInt64>{}(id_);
        }
        
        bool isValid() const noexcept {
            return id_ != 0;
        }
        
        static TypedID invalid() {
            return TypedID(0);
        }
    };

    // Common ID types
    struct ProcessingUnitTag {};
    struct SystemTag {};
    struct PortTag {};
    struct TaskTag {};
    
    using ProcessingUnitID = TypedID<ProcessingUnitTag>;
    using SystemID = TypedID<SystemTag>;
    using PortID = TypedID<PortTag>;
    using TaskID = TypedID<TaskTag>;

} // namespace axonvex::types::primitives

// Hash specializations for std::unordered_map support
namespace std {
    template<>
    struct hash<axonvex::types::primitives::UUID> {
        std::size_t operator()(const axonvex::types::primitives::UUID& uuid) const noexcept {
            return uuid.hash();
        }
    };
    
    template<typename Tag>
    struct hash<axonvex::types::primitives::TypedID<Tag>> {
        std::size_t operator()(const axonvex::types::primitives::TypedID<Tag>& id) const noexcept {
            return id.hash();
        }
    };
}