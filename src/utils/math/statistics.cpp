/**
 * @file statistics.cpp
 * @brief Statistical analysis utilities implementation
 */

#include <axonvex/utils/math/statistics.hpp>
#include <algorithm>
#include <cmath>

namespace axonvex::utils::math {

double Statistics::mean(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    double sum = 0.0;
    for (double val : data) sum += val;
    return sum / data.size();
}

double Statistics::variance(const std::vector<double>& data) {
    if (data.size() < 2) return 0.0;
    double m = mean(data);
    double sum = 0.0;
    for (double val : data) {
        double diff = val - m;
        sum += diff * diff;
    }
    return sum / (data.size() - 1);
}

double Statistics::standardDeviation(const std::vector<double>& data) {
    return std::sqrt(variance(data));
}

double Statistics::median(std::vector<double> data) {
    if (data.empty()) return 0.0;
    
    std::sort(data.begin(), data.end());
    size_t n = data.size();
    if (n % 2 == 0) {
        return (data[n/2 - 1] + data[n/2]) / 2.0;
    } else {
        return data[n/2];
    }
}

double Statistics::min(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    return *std::min_element(data.begin(), data.end());
}

double Statistics::max(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    return *std::max_element(data.begin(), data.end());
}

} // namespace axonvex::utils::math