/**
 * @file jsonSerializer.cpp
 * @brief JSON serialization implementation - stub
 */

#include <axonvex/utils/serialization/jsonSerializer.hpp>

namespace axonvex::utils::serialization {

bool JSONSerializer::validate(const std::string& json) {
    // Simple validation - check for balanced braces/brackets
    int braces = 0, brackets = 0;
    for (char c : json) {
        if (c == '{') braces++;
        else if (c == '}') braces--;
        else if (c == '[') brackets++;
        else if (c == ']') brackets--;
    }
    return braces == 0 && brackets == 0;
}

} // namespace axonvex::utils::serialization