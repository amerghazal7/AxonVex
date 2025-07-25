/**
 * @file jsonSerializer.hpp
 * @brief JSON serialization utilities
 */

#pragma once

#include <string>

namespace axonvex::utils::serialization {

class JSONSerializer {
public:
    template<typename T>
    static std::string serialize(const T& obj);
    
    template<typename T>
    static T deserialize(const std::string& json);
    
    static bool validate(const std::string& json);
};

} // namespace axonvex::utils::serialization