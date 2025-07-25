/**
 * @file schemaValidator.hpp
 * @brief Data validation and schema utilities
 */

#pragma once

#include <string>

namespace axonvex::utils::validation {

class SchemaValidator {
public:
    static bool validate(const std::string& data, const std::string& schema);
    static bool validateJSON(const std::string& json);
    static bool validateXML(const std::string& xml);
};

} // namespace axonvex::utils::validation