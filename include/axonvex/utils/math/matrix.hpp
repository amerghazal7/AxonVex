/**
 * @file matrix.hpp
 * @brief High-performance matrix operations with SIMD optimization
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 */

#pragma once

#include <vector>
#include <memory>
#include <cstddef>
#include <stdexcept>
#include <initializer_list>

namespace axonvex::utils::math {

/**
 * @class Matrix
 * @brief High-performance matrix class with SIMD optimization
 *
 * Performance-optimized matrix implementation designed for real-time applications.
 * Features include:
 * - SIMD-optimized operations for maximum performance
 * - Memory-efficient storage with configurable alignment
 * - Support for both dense and sparse operations
 * - Real-time memory allocation strategies
 * - Thread-safe operations where applicable
 *
 * Performance targets:
 * - Matrix multiplication: <1ms for 100x100 matrices
 * - Memory overhead: <5% compared to raw arrays
 * - SIMD utilization: >90% for applicable operations
 */
class Matrix {
public:
    // Type definitions
    using value_type = double;
    using size_type = std::size_t;
    using reference = value_type&;
    using const_reference = const value_type&;

    // Constructors and destructor
    /**
     * @brief Construct matrix with specified dimensions
     * @param rows Number of rows
     * @param cols Number of columns
     */
    Matrix(size_type rows, size_type cols);

    /**
     * @brief Construct matrix with dimensions and initial value
     * @param rows Number of rows
     * @param cols Number of columns
     * @param value Initial value for all elements
     */
    Matrix(size_type rows, size_type cols, value_type value);

    /**
     * @brief Construct matrix from initializer list
     * @param init 2D initializer list
     */
    Matrix(std::initializer_list<std::initializer_list<value_type>> init);

    /**
     * @brief Copy constructor
     */
    Matrix(const Matrix& other);

    /**
     * @brief Move constructor
     */
    Matrix(Matrix&& other) noexcept;

    /**
     * @brief Destructor
     */
    ~Matrix();

    // Assignment operators
    /**
     * @brief Copy assignment operator
     */
    Matrix& operator=(const Matrix& other);

    /**
     * @brief Move assignment operator
     */
    Matrix& operator=(Matrix&& other) noexcept;

    // Basic operations
    /**
     * @brief Matrix multiplication (optimized with SIMD)
     * @param other Right-hand side matrix
     * @return Result matrix
     */
    Matrix operator*(const Matrix& other) const;

    /**
     * @brief Matrix addition
     * @param other Right-hand side matrix
     * @return Result matrix
     */
    Matrix operator+(const Matrix& other) const;

    /**
     * @brief Matrix subtraction
     * @param other Right-hand side matrix
     * @return Result matrix
     */
    Matrix operator-(const Matrix& other) const;

    /**
     * @brief Scalar multiplication
     * @param scalar Scalar value
     * @return Result matrix
     */
    Matrix operator*(value_type scalar) const;

    /**
     * @brief Scalar division
     * @param scalar Scalar value
     * @return Result matrix
     */
    Matrix operator/(value_type scalar) const;

    // In-place operations
    /**
     * @brief In-place matrix addition
     */
    Matrix& operator+=(const Matrix& other);

    /**
     * @brief In-place matrix subtraction
     */
    Matrix& operator-=(const Matrix& other);

    /**
     * @brief In-place scalar multiplication
     */
    Matrix& operator*=(value_type scalar);

    /**
     * @brief In-place scalar division
     */
    Matrix& operator/=(value_type scalar);

    // Element access
    /**
     * @brief Access element at (row, col) with bounds checking
     * @param row Row index
     * @param col Column index
     * @return Reference to element
     */
    reference operator()(size_type row, size_type col);

    /**
     * @brief Access element at (row, col) with bounds checking (const)
     * @param row Row index
     * @param col Column index
     * @return Const reference to element
     */
    const_reference operator()(size_type row, size_type col) const;

    /**
     * @brief Access element at (row, col) without bounds checking
     * @param row Row index
     * @param col Column index
     * @return Reference to element
     */
    reference at(size_type row, size_type col);

    /**
     * @brief Access element at (row, col) without bounds checking (const)
     * @param row Row index
     * @param col Column index
     * @return Const reference to element
     */
    const_reference at(size_type row, size_type col) const;

    // Utility methods
    /**
     * @brief Get number of rows
     * @return Number of rows
     */
    size_type rows() const noexcept { return rows_; }

    /**
     * @brief Get number of columns
     * @return Number of columns
     */
    size_type cols() const noexcept { return cols_; }

    /**
     * @brief Check if matrix is square
     * @return True if square matrix
     */
    bool isSquare() const noexcept { return rows_ == cols_; }

    /**
     * @brief Check if matrix is empty
     * @return True if empty
     */
    bool empty() const noexcept { return rows_ == 0 || cols_ == 0; }

    /**
     * @brief Get total number of elements
     * @return Total elements
     */
    size_type size() const noexcept { return rows_ * cols_; }

    // Advanced operations
    /**
     * @brief Compute matrix transpose
     * @return Transposed matrix
     */
    Matrix transpose() const;

    /**
     * @brief Compute matrix inverse (for square matrices)
     * @return Inverse matrix
     * @throws std::runtime_error if matrix is not invertible
     */
    Matrix inverse() const;

    /**
     * @brief Compute matrix determinant (for square matrices)
     * @return Determinant value
     * @throws std::runtime_error if matrix is not square
     */
    value_type determinant() const;

    /**
     * @brief Compute matrix trace (sum of diagonal elements)
     * @return Trace value
     * @throws std::runtime_error if matrix is not square
     */
    value_type trace() const;

    /**
     * @brief Compute Frobenius norm
     * @return Frobenius norm
     */
    value_type frobeniusNorm() const;

    // Performance optimization methods
    /**
     * @brief Enable/disable SIMD optimization
     * @param enable True to enable SIMD
     */
    void enableSIMD(bool enable) noexcept { simdEnabled_ = enable; }

    /**
     * @brief Check if SIMD is enabled
     * @return True if SIMD is enabled
     */
    bool isSIMDEnabled() const noexcept { return simdEnabled_; }

    /**
     * @brief Optimize memory layout for better cache performance
     */
    void optimizeMemoryLayout();

    /**
     * @brief Set optimization level (0-3)
     * @param level Optimization level
     */
    void setOptimizationLevel(int level) noexcept { optimizationLevel_ = level; }

    /**
     * @brief Get current optimization level
     * @return Optimization level
     */
    int getOptimizationLevel() const noexcept { return optimizationLevel_; }

    // Memory management
    /**
     * @brief Reserve memory for matrix operations
     * @param size Number of elements to reserve
     */
    void reserve(size_type size);

    /**
     * @brief Resize matrix (may invalidate data)
     * @param newRows New number of rows
     * @param newCols New number of columns
     */
    void resize(size_type newRows, size_type newCols);

    /**
     * @brief Fill matrix with specified value
     * @param value Fill value
     */
    void fill(value_type value);

    /**
     * @brief Set matrix to zero
     */
    void setZero();

    /**
     * @brief Set matrix to identity (for square matrices)
     * @throws std::runtime_error if matrix is not square
     */
    void setIdentity();

    // Data access
    /**
     * @brief Get pointer to raw data
     * @return Pointer to data
     */
    value_type* data() noexcept { return data_.get(); }

    /**
     * @brief Get const pointer to raw data
     * @return Const pointer to data
     */
    const value_type* data() const noexcept { return data_.get(); }

    // Static factory methods
    /**
     * @brief Create zero matrix
     * @param rows Number of rows
     * @param cols Number of columns
     * @return Zero matrix
     */
    static Matrix zeros(size_type rows, size_type cols);

    /**
     * @brief Create ones matrix
     * @param rows Number of rows
     * @param cols Number of columns
     * @return Ones matrix
     */
    static Matrix ones(size_type rows, size_type cols);

    /**
     * @brief Create identity matrix
     * @param size Matrix size (square)
     * @return Identity matrix
     */
    static Matrix identity(size_type size);

    /**
     * @brief Create random matrix with values in [0, 1]
     * @param rows Number of rows
     * @param cols Number of columns
     * @return Random matrix
     */
    static Matrix random(size_type rows, size_type cols);

private:
    size_type rows_;                    ///< Number of rows
    size_type cols_;                    ///< Number of columns
    std::unique_ptr<value_type[]> data_;///< Matrix data (aligned for SIMD)
    bool simdEnabled_;                  ///< SIMD optimization flag
    int optimizationLevel_;             ///< Optimization level (0-3)
    size_t alignment_;                  ///< Memory alignment for SIMD

    // Internal helper methods
    void allocateMemory();
    void deallocateMemory();
    void copyData(const Matrix& other);
    void moveData(Matrix&& other) noexcept;
    bool isValidIndex(size_type row, size_type col) const noexcept;
    void checkDimensions(const Matrix& other, const char* operation) const;
    void checkSquare(const char* operation) const;

    // SIMD-optimized internal operations
    void simdMultiply(const Matrix& a, const Matrix& b, Matrix& result) const;
    void simdAdd(const Matrix& a, const Matrix& b, Matrix& result) const;
    void simdSubtract(const Matrix& a, const Matrix& b, Matrix& result) const;
    void simdScalarMultiply(const Matrix& a, value_type scalar, Matrix& result) const;

    // Memory alignment helpers
    static constexpr size_t getAlignment() noexcept;
    static value_type* allocateAligned(size_type size, size_t alignment);
    static void deallocateAligned(value_type* ptr) noexcept;
};

// Non-member operators
/**
 * @brief Scalar multiplication (scalar * matrix)
 */
Matrix operator*(Matrix::value_type scalar, const Matrix& matrix);

/**
 * @brief Stream output operator
 */
std::ostream& operator<<(std::ostream& os, const Matrix& matrix);

/**
 * @brief Equality comparison
 */
bool operator==(const Matrix& lhs, const Matrix& rhs);

/**
 * @brief Inequality comparison
 */
bool operator!=(const Matrix& lhs, const Matrix& rhs);

} // namespace axonvex::utils::math