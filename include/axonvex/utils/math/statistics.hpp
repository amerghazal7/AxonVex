/**
 * @file statistics.hpp
 * @brief Statistical analysis utilities
 */

#pragma once

#include <vector>

namespace axonvex::utils::math {

class Statistics {
public:
    static double mean(const std::vector<double>& data);
    static double variance(const std::vector<double>& data);
    static double standardDeviation(const std::vector<double>& data);
    static double median(std::vector<double> data);
    static double min(const std::vector<double>& data);
    static double max(const std::vector<double>& data);
};

} // namespace axonvex::utils::math