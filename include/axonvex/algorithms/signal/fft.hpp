/**
 * @brief FFT Implementation
 */
#pragma once
#include <axonvex/core/processingUnit.hpp>
#include <vector>

namespace axonvex::algorithms::signal {
class FFT : public core::ProcessingUnit {
public:
    explicit FFT(const std::string& name = "FFT") : ProcessingUnit(name) {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    std::string getTypeDescription() override { return "FFT"; }
};
} // namespace axonvex::algorithms::signal