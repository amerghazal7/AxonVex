/**
 * @file binarySerializer.hpp
 * @brief Binary serialization utilities
 */

#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace axonvex::utils::serialization {

class BinarySerializer {
public:
    template<typename T>
    static std::vector<uint8_t> serialize(const T& obj);
    
    template<typename T>
    static T deserialize(const std::vector<uint8_t>& data);
    
    static std::vector<uint8_t> serializeString(const std::string& str);
    static std::string deserializeString(const std::vector<uint8_t>& data);
};

} // namespace axonvex::utils::serialization