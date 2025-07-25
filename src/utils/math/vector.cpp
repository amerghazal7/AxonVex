/**
 * @file vector.cpp
 * @brief Vector class implementation
 */

#include <axonvex/utils/math/vector.hpp>
#include <cmath>
#include <stdexcept>

namespace axonvex::utils::math {

Vector::Vector(size_type size) : data_(size, 0.0) {}

Vector::Vector(const std::vector<value_type>& data) : data_(data) {}

Vector::Vector(std::initializer_list<value_type> init) : data_(init) {}

Vector Vector::operator+(const Vector& other) const {
    if (size() != other.size()) {
        throw std::invalid_argument("Vector dimensions must match for addition");
    }
    
    Vector result(size());
    for (size_type i = 0; i < size(); ++i) {
        result[i] = data_[i] + other[i];
    }
    return result;
}

Vector Vector::operator-(const Vector& other) const {
    if (size() != other.size()) {
        throw std::invalid_argument("Vector dimensions must match for subtraction");
    }
    
    Vector result(size());
    for (size_type i = 0; i < size(); ++i) {
        result[i] = data_[i] - other[i];
    }
    return result;
}

Vector Vector::operator*(value_type scalar) const {
    Vector result(size());
    for (size_type i = 0; i < size(); ++i) {
        result[i] = data_[i] * scalar;
    }
    return result;
}

Vector::value_type Vector::dot(const Vector& other) const {
    if (size() != other.size()) {
        throw std::invalid_argument("Vector dimensions must match for dot product");
    }
    
    value_type result = 0.0;
    for (size_type i = 0; i < size(); ++i) {
        result += data_[i] * other[i];
    }
    return result;
}

Vector::value_type Vector::norm() const {
    return std::sqrt(dot(*this));
}

Vector Vector::normalize() const {
    value_type n = norm();
    if (n == 0.0) {
        throw std::runtime_error("Cannot normalize zero vector");
    }
    return (*this) * (1.0 / n);
}

Vector::value_type& Vector::operator[](size_type index) {
    return data_[index];
}

const Vector::value_type& Vector::operator[](size_type index) const {
    return data_[index];
}

void Vector::resize(size_type newSize) {
    data_.resize(newSize);
}

void Vector::clear() {
    data_.clear();
}

bool Vector::isEmpty() const {
    return data_.empty();
}

Vector Vector::cross(const Vector& other) const {
    if (size() != 3 || other.size() != 3) {
        throw std::invalid_argument("Cross product only defined for 3D vectors");
    }
    
    Vector result(3);
    result[0] = data_[1] * other[2] - data_[2] * other[1];
    result[1] = data_[2] * other[0] - data_[0] * other[2];
    result[2] = data_[0] * other[1] - data_[1] * other[0];
    return result;
}

} // namespace axonvex::utils::math