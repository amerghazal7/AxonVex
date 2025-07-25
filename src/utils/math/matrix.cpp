/**
 * @file matrix.cpp
 * @brief High-performance matrix operations implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <axonvex/utils/math/matrix.hpp>
#include <axonvex/core/precisionTimer.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <random>
#include <iostream>
#include <iomanip>

// SIMD includes (conditionally compiled based on platform and availability)
#ifdef AXONVEX_PLATFORM_LINUX
    #if defined(__AVX__) || defined(__AVX2__)
        #include <immintrin.h>  // AVX/SSE
        #define AXONVEX_HAS_AVX 1
    #elif defined(__SSE2__)
        #include <emmintrin.h>  // SSE2
        #define AXONVEX_HAS_SSE2 1
    #endif
#elif defined(AXONVEX_PLATFORM_WINDOWS)
    #if defined(__AVX__) || defined(__AVX2__)
        #include <intrin.h>
        #define AXONVEX_HAS_AVX 1
    #elif defined(__SSE2__)
        #include <intrin.h>
        #define AXONVEX_HAS_SSE2 1
    #endif
#elif defined(AXONVEX_PLATFORM_MACOS)
    #if defined(__AVX__) || defined(__AVX2__)
        #include <immintrin.h>
        #define AXONVEX_HAS_AVX 1
    #elif defined(__SSE2__)
        #include <emmintrin.h>
        #define AXONVEX_HAS_SSE2 1
    #endif
#endif

namespace axonvex::utils::math {

// Constants for SIMD optimization
constexpr size_t SIMD_ALIGNMENT = 32;  // 256-bit alignment for AVX
constexpr size_t SIMD_WIDTH = 4;       // 4 doubles per AVX vector

// Constructor implementations
Matrix::Matrix(size_type rows, size_type cols)
    : rows_(rows), cols_(cols), data_(nullptr), simdEnabled_(true), 
      optimizationLevel_(2), alignment_(SIMD_ALIGNMENT) {
    if (rows == 0 || cols == 0) {
        throw std::invalid_argument("Matrix dimensions must be positive");
    }
    allocateMemory();
    setZero();
}

Matrix::Matrix(size_type rows, size_type cols, value_type value)
    : rows_(rows), cols_(cols), data_(nullptr), simdEnabled_(true),
      optimizationLevel_(2), alignment_(SIMD_ALIGNMENT) {
    if (rows == 0 || cols == 0) {
        throw std::invalid_argument("Matrix dimensions must be positive");
    }
    allocateMemory();
    fill(value);
}

Matrix::Matrix(std::initializer_list<std::initializer_list<value_type>> init)
    : rows_(init.size()), cols_(init.size() > 0 ? init.begin()->size() : 0),
      data_(nullptr), simdEnabled_(true), optimizationLevel_(2), alignment_(SIMD_ALIGNMENT) {
    
    if (rows_ == 0 || cols_ == 0) {
        throw std::invalid_argument("Matrix cannot be empty");
    }
    
    // Check that all rows have the same length
    for (const auto& row : init) {
        if (row.size() != cols_) {
            throw std::invalid_argument("All matrix rows must have the same length");
        }
    }
    
    allocateMemory();
    
    size_type row = 0;
    for (const auto& rowData : init) {
        size_type col = 0;
        for (const auto& val : rowData) {
            (*this)(row, col) = val;
            ++col;
        }
        ++row;
    }
}

Matrix::Matrix(const Matrix& other)
    : rows_(other.rows_), cols_(other.cols_), data_(nullptr),
      simdEnabled_(other.simdEnabled_), optimizationLevel_(other.optimizationLevel_),
      alignment_(other.alignment_) {
    allocateMemory();
    copyData(other);
}

Matrix::Matrix(Matrix&& other) noexcept
    : rows_(0), cols_(0), data_(nullptr), simdEnabled_(true),
      optimizationLevel_(2), alignment_(SIMD_ALIGNMENT) {
    moveData(std::move(other));
}

Matrix::~Matrix() {
    deallocateMemory();
}

// Assignment operators
Matrix& Matrix::operator=(const Matrix& other) {
    if (this != &other) {
        deallocateMemory();
        rows_ = other.rows_;
        cols_ = other.cols_;
        simdEnabled_ = other.simdEnabled_;
        optimizationLevel_ = other.optimizationLevel_;
        alignment_ = other.alignment_;
        allocateMemory();
        copyData(other);
    }
    return *this;
}

Matrix& Matrix::operator=(Matrix&& other) noexcept {
    if (this != &other) {
        deallocateMemory();
        moveData(std::move(other));
    }
    return *this;
}

// Basic operations
Matrix Matrix::operator*(const Matrix& other) const {
    if (cols_ != other.rows_) {
        throw std::invalid_argument("Matrix dimensions incompatible for multiplication");
    }
    
    Matrix result(rows_, other.cols_);
    
    if (simdEnabled_ && optimizationLevel_ >= 2) {
        simdMultiply(*this, other, result);
    } else {
        // Standard multiplication
        for (size_type i = 0; i < rows_; ++i) {
            for (size_type j = 0; j < other.cols_; ++j) {
                value_type sum = 0.0;
                for (size_type k = 0; k < cols_; ++k) {
                    sum += (*this)(i, k) * other(k, j);
                }
                result(i, j) = sum;
            }
        }
    }
    
    return result;
}

Matrix Matrix::operator+(const Matrix& other) const {
    checkDimensions(other, "addition");
    Matrix result(rows_, cols_);
    
    if (simdEnabled_ && optimizationLevel_ >= 1) {
        simdAdd(*this, other, result);
    } else {
        for (size_type i = 0; i < size(); ++i) {
            result.data_[i] = data_[i] + other.data_[i];
        }
    }
    
    return result;
}

Matrix Matrix::operator-(const Matrix& other) const {
    checkDimensions(other, "subtraction");
    Matrix result(rows_, cols_);
    
    if (simdEnabled_ && optimizationLevel_ >= 1) {
        simdSubtract(*this, other, result);
    } else {
        for (size_type i = 0; i < size(); ++i) {
            result.data_[i] = data_[i] - other.data_[i];
        }
    }
    
    return result;
}

Matrix Matrix::operator*(value_type scalar) const {
    Matrix result(rows_, cols_);
    
    if (simdEnabled_ && optimizationLevel_ >= 1) {
        simdScalarMultiply(*this, scalar, result);
    } else {
        for (size_type i = 0; i < size(); ++i) {
            result.data_[i] = data_[i] * scalar;
        }
    }
    
    return result;
}

// Element access
Matrix::reference Matrix::operator()(size_type row, size_type col) {
    if (!isValidIndex(row, col)) {
        throw std::out_of_range("Matrix index out of range");
    }
    return data_[row * cols_ + col];
}

Matrix::const_reference Matrix::operator()(size_type row, size_type col) const {
    if (!isValidIndex(row, col)) {
        throw std::out_of_range("Matrix index out of range");
    }
    return data_[row * cols_ + col];
}

// Advanced operations
Matrix Matrix::transpose() const {
    Matrix result(cols_, rows_);
    
    for (size_type i = 0; i < rows_; ++i) {
        for (size_type j = 0; j < cols_; ++j) {
            result(j, i) = (*this)(i, j);
        }
    }
    
    return result;
}

Matrix::value_type Matrix::determinant() const {
    checkSquare("determinant calculation");
    
    if (rows_ == 1) {
        return (*this)(0, 0);
    }
    
    if (rows_ == 2) {
        return (*this)(0, 0) * (*this)(1, 1) - (*this)(0, 1) * (*this)(1, 0);
    }
    
    // For larger matrices, use LU decomposition or recursive cofactor expansion
    // This is a simplified implementation for demonstration
    value_type det = 0.0;
    for (size_type j = 0; j < cols_; ++j) {
        // Create minor matrix
        Matrix minor(rows_ - 1, cols_ - 1);
        for (size_type i = 1; i < rows_; ++i) {
            size_type minorCol = 0;
            for (size_type k = 0; k < cols_; ++k) {
                if (k != j) {
                    minor(i - 1, minorCol) = (*this)(i, k);
                    ++minorCol;
                }
            }
        }
        
        value_type cofactor = ((j % 2 == 0) ? 1.0 : -1.0) * (*this)(0, j) * minor.determinant();
        det += cofactor;
    }
    
    return det;
}

Matrix::value_type Matrix::trace() const {
    checkSquare("trace calculation");
    
    value_type tr = 0.0;
    for (size_type i = 0; i < rows_; ++i) {
        tr += (*this)(i, i);
    }
    
    return tr;
}

// Utility methods
void Matrix::fill(value_type value) {
    std::fill_n(data_.get(), size(), value);
}

void Matrix::setZero() {
    fill(0.0);
}

void Matrix::setIdentity() {
    checkSquare("identity matrix creation");
    
    setZero();
    for (size_type i = 0; i < rows_; ++i) {
        (*this)(i, i) = 1.0;
    }
}

// Static factory methods
Matrix Matrix::zeros(size_type rows, size_type cols) {
    return Matrix(rows, cols, 0.0);
}

Matrix Matrix::ones(size_type rows, size_type cols) {
    return Matrix(rows, cols, 1.0);
}

Matrix Matrix::identity(size_type size) {
    Matrix result(size, size);
    result.setIdentity();
    return result;
}

Matrix Matrix::random(size_type rows, size_type cols) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<value_type> dis(0.0, 1.0);
    
    Matrix result(rows, cols);
    for (size_type i = 0; i < result.size(); ++i) {
        result.data_[i] = dis(gen);
    }
    
    return result;
}

// Private helper methods
void Matrix::allocateMemory() {
    if (size() == 0) {
        data_ = nullptr;
        return;
    }
    
    data_.reset(allocateAligned(size(), alignment_));
    if (!data_) {
        throw std::bad_alloc();
    }
}

void Matrix::deallocateMemory() {
    if (data_) {
        deallocateAligned(data_.release());
    }
}

void Matrix::copyData(const Matrix& other) {
    if (size() != other.size()) {
        throw std::runtime_error("Matrix size mismatch during copy");
    }
    
    std::memcpy(data_.get(), other.data_.get(), size() * sizeof(value_type));
}

void Matrix::moveData(Matrix&& other) noexcept {
    rows_ = other.rows_;
    cols_ = other.cols_;
    data_ = std::move(other.data_);
    simdEnabled_ = other.simdEnabled_;
    optimizationLevel_ = other.optimizationLevel_;
    alignment_ = other.alignment_;
    
    other.rows_ = 0;
    other.cols_ = 0;
}

bool Matrix::isValidIndex(size_type row, size_type col) const noexcept {
    return row < rows_ && col < cols_;
}

void Matrix::checkDimensions(const Matrix& other, const char* operation) const {
    if (rows_ != other.rows_ || cols_ != other.cols_) {
        throw std::invalid_argument(std::string("Matrix dimensions incompatible for ") + operation);
    }
}

void Matrix::checkSquare(const char* operation) const {
    if (!isSquare()) {
        throw std::runtime_error(std::string("Matrix must be square for ") + operation);
    }
}

// SIMD-optimized operations
void Matrix::simdMultiply(const Matrix& a, const Matrix& b, Matrix& result) const {
    // For now, use standard multiplication until SIMD detection is properly configured
    // TODO: Properly implement AVX detection and compilation flags
    for (size_type i = 0; i < a.rows_; ++i) {
        for (size_type j = 0; j < b.cols_; ++j) {
            value_type sum = 0.0;
            for (size_type k = 0; k < a.cols_; ++k) {
                sum += a(i, k) * b(k, j);
            }
            result(i, j) = sum;
        }
    }
}

void Matrix::simdAdd(const Matrix& a, const Matrix& b, Matrix& result) const {
    // Fallback to standard addition for now
    for (size_type i = 0; i < a.size(); ++i) {
        result.data_[i] = a.data_[i] + b.data_[i];
    }
}

void Matrix::simdSubtract(const Matrix& a, const Matrix& b, Matrix& result) const {
#ifdef AXONVEX_HAS_AVX
    size_type simd_size = (a.size() / SIMD_WIDTH) * SIMD_WIDTH;
    
    for (size_type i = 0; i < simd_size; i += SIMD_WIDTH) {
        __m256d a_vals = _mm256_load_pd(&a.data_[i]);
        __m256d b_vals = _mm256_load_pd(&b.data_[i]);
        __m256d result_vals = _mm256_sub_pd(a_vals, b_vals);
        _mm256_store_pd(&result.data_[i], result_vals);
    }
    
    // Handle remaining elements
    for (size_type i = simd_size; i < a.size(); ++i) {
        result.data_[i] = a.data_[i] - b.data_[i];
    }
#else
    // Fallback to standard subtraction
    for (size_type i = 0; i < a.size(); ++i) {
        result.data_[i] = a.data_[i] - b.data_[i];
    }
#endif
}

void Matrix::simdScalarMultiply(const Matrix& a, value_type scalar, Matrix& result) const {
#ifdef AXONVEX_HAS_AVX
    size_type simd_size = (a.size() / SIMD_WIDTH) * SIMD_WIDTH;
    __m256d scalar_vec = _mm256_set1_pd(scalar);
    
    for (size_type i = 0; i < simd_size; i += SIMD_WIDTH) {
        __m256d a_vals = _mm256_load_pd(&a.data_[i]);
        __m256d result_vals = _mm256_mul_pd(a_vals, scalar_vec);
        _mm256_store_pd(&result.data_[i], result_vals);
    }
    
    // Handle remaining elements
    for (size_type i = simd_size; i < a.size(); ++i) {
        result.data_[i] = a.data_[i] * scalar;
    }
#else
    // Fallback to standard scalar multiplication
    for (size_type i = 0; i < a.size(); ++i) {
        result.data_[i] = a.data_[i] * scalar;
    }
#endif
}

// Memory alignment helpers
constexpr size_t Matrix::getAlignment() noexcept {
    return SIMD_ALIGNMENT;
}

Matrix::value_type* Matrix::allocateAligned(size_type size, size_t alignment) {
#ifdef AXONVEX_PLATFORM_LINUX
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size * sizeof(value_type)) != 0) {
        return nullptr;
    }
    return static_cast<value_type*>(ptr);
#elif defined(AXONVEX_PLATFORM_WINDOWS)
    return static_cast<value_type*>(_aligned_malloc(size * sizeof(value_type), alignment));
#else
    // Fallback to standard allocation
    return new value_type[size];
#endif
}

void Matrix::deallocateAligned(value_type* ptr) noexcept {
    if (!ptr) return;
    
#ifdef AXONVEX_PLATFORM_LINUX
    free(ptr);
#elif defined(AXONVEX_PLATFORM_WINDOWS)
    _aligned_free(ptr);
#else
    delete[] ptr;
#endif
}

// Non-member operators
Matrix operator*(Matrix::value_type scalar, const Matrix& matrix) {
    return matrix * scalar;
}

std::ostream& operator<<(std::ostream& os, const Matrix& matrix) {
    os << std::fixed << std::setprecision(6);
    for (Matrix::size_type i = 0; i < matrix.rows(); ++i) {
        os << "[";
        for (Matrix::size_type j = 0; j < matrix.cols(); ++j) {
            os << std::setw(12) << matrix(i, j);
            if (j < matrix.cols() - 1) os << ", ";
        }
        os << "]";
        if (i < matrix.rows() - 1) os << "\n";
    }
    return os;
}

bool operator==(const Matrix& lhs, const Matrix& rhs) {
    if (lhs.rows() != rhs.rows() || lhs.cols() != rhs.cols()) {
        return false;
    }
    
    constexpr double epsilon = 1e-9;
    for (Matrix::size_type i = 0; i < lhs.size(); ++i) {
        if (std::abs(lhs.data()[i] - rhs.data()[i]) > epsilon) {
            return false;
        }
    }
    
    return true;
}

bool operator!=(const Matrix& lhs, const Matrix& rhs) {
    return !(lhs == rhs);
}

} // namespace axonvex::utils::math