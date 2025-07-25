/**
 * @file matrix_example.cpp
 * @brief Example demonstrating the new Matrix class from the Utils module
 */

#include <axonvex/axonvex.hpp>
#include <iostream>

int main() {
    std::cout << "=== AxonVex Phase 3 Modular Architecture Demo ===" << std::endl;
    std::cout << "Testing the new Utils module with Matrix class" << std::endl << std::endl;

#ifdef AXONVEX_UTILS_MODULE_AVAILABLE
    std::cout << "✓ Utils module is available!" << std::endl;
    
    // Test Matrix operations
    std::cout << "\n--- Matrix Operations Test ---" << std::endl;
    
    // Create matrices using different constructors
    axonvex::utils::math::Matrix a(2, 2, 1.5);  // 2x2 matrix filled with 1.5
    axonvex::utils::math::Matrix b = axonvex::utils::math::Matrix::identity(2);  // 2x2 identity matrix
    
    std::cout << "Matrix A (2x2, filled with 1.5):" << std::endl;
    std::cout << a << std::endl << std::endl;
    
    std::cout << "Matrix B (2x2 identity):" << std::endl;
    std::cout << b << std::endl << std::endl;
    
    // Test matrix operations
    auto sum = a + b;
    std::cout << "A + B:" << std::endl;
    std::cout << sum << std::endl << std::endl;
    
    auto product = a * b;
    std::cout << "A * B:" << std::endl;
    std::cout << product << std::endl << std::endl;
    
    auto scaled = a * 2.0;
    std::cout << "A * 2.0:" << std::endl;
    std::cout << scaled << std::endl << std::endl;
    
    // Test initializer list constructor
    axonvex::utils::math::Matrix c = {
        {1.0, 2.0},
        {3.0, 4.0}
    };
    
    std::cout << "Matrix C (from initializer list):" << std::endl;
    std::cout << c << std::endl << std::endl;
    
    std::cout << "C transpose:" << std::endl;
    std::cout << c.transpose() << std::endl << std::endl;
    
    std::cout << "C determinant: " << c.determinant() << std::endl;
    std::cout << "C trace: " << c.trace() << std::endl << std::endl;
    
    // Test Vector operations
    std::cout << "\n--- Vector Operations Test ---" << std::endl;
    
    axonvex::utils::math::Vector v1({1.0, 2.0, 3.0});
    axonvex::utils::math::Vector v2({4.0, 5.0, 6.0});
    
    std::cout << "Vector v1: [1, 2, 3]" << std::endl;
    std::cout << "Vector v2: [4, 5, 6]" << std::endl;
    
    auto v_sum = v1 + v2;
    std::cout << "v1 + v2: [" << v_sum[0] << ", " << v_sum[1] << ", " << v_sum[2] << "]" << std::endl;
    
    auto dot_product = v1.dot(v2);
    std::cout << "v1 · v2 (dot product): " << dot_product << std::endl;
    
    std::cout << "||v1|| (norm): " << v1.norm() << std::endl << std::endl;
    
    // Test Performance Profiler
    std::cout << "\n--- Performance Profiler Test ---" << std::endl;
    
    axonvex::utils::profiling::PerformanceProfiler::startTimer("matrix_multiply");
    
    // Perform some matrix operations
    auto large_a = axonvex::utils::math::Matrix::random(10, 10);
    auto large_b = axonvex::utils::math::Matrix::random(10, 10);
    auto large_result = large_a * large_b;
    
    auto duration = axonvex::utils::profiling::PerformanceProfiler::stopTimer("matrix_multiply");
    
    std::cout << "10x10 matrix multiplication took: " 
              << duration.count() << " nanoseconds" << std::endl;
    
    axonvex::utils::profiling::PerformanceProfiler::printResults();
    
#else
    std::cout << "✗ Utils module is not available" << std::endl;
#endif

    std::cout << "\n=== Phase 3 Modular Architecture Test Complete ===" << std::endl;
    return 0;
}