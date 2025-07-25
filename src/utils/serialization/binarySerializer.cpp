/**
 * @file binarySerializer.cpp
 * @brief Binary serialization implementation - simplified stub
 */

#include <axonvex/utils/serialization/binarySerializer.hpp>
#include <cstring>

namespace axonvex::utils::serialization {

std::vector<uint8_t> BinarySerializer::serializeString(const std::string& str) {
    std::vector<uint8_t> result(str.size() + sizeof(size_t));
    size_t size = str.size();
    std::memcpy(result.data(), &size, sizeof(size_t));
    std::memcpy(result.data() + sizeof(size_t), str.data(), str.size());
    return result;
}

std::string BinarySerializer::deserializeString(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(size_t)) return "";
    size_t size;
    std::memcpy(&size, data.data(), sizeof(size_t));
    if (data.size() < sizeof(size_t) + size) return "";
    return std::string(reinterpret_cast<const char*>(data.data() + sizeof(size_t)), size);
}

} // namespace axonvex::utils::serialization