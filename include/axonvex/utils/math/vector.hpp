/**
 * @file vector.hpp
 * @brief Optimized vector operations
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <vector>
#include <cstddef>
#include <initializer_list>

namespace axonvex::utils::math {

/**
 * @class Vector
 * @brief High-performance vector class with mathematical operations
 */
class Vector {
public:
    using value_type = double;
    using size_type = std::size_t;

    explicit Vector(size_type size);
    Vector(const std::vector<value_type>& data);
    Vector(std::initializer_list<value_type> init);
    
    // Basic operations
    Vector operator+(const Vector& other) const;
    Vector operator-(const Vector& other) const;
    Vector operator*(value_type scalar) const;
    
    // Mathematical operations
    value_type dot(const Vector& other) const;
    Vector cross(const Vector& other) const;
    value_type norm() const;
    Vector normalize() const;
    
    // Accessors
    value_type& operator[](size_type index);
    const value_type& operator[](size_type index) const;
    size_type size() const { return data_.size(); }
    
    // Utility methods
    void resize(size_type newSize);
    void clear();
    bool isEmpty() const;

private:
    std::vector<value_type> data_;
};

} // namespace axonvex::utils::math