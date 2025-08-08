#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <filesystem>

namespace axonvex::utils::serialization {

class JSONSerializer {
public:
    using json = nlohmann::json;

    static std::string toString(const json& j, int indent = 2) {
        return j.dump(indent);
    }

    static json fromString(const std::string& s) {
        return json::parse(s);
    }

    template <typename T>
    static std::string serialize(const T& obj, int indent = 2) {
        json j = obj; // relies on to_json overload
        return toString(j, indent);
    }

    template <typename T>
    static T deserialize(const std::string& s) {
        json j = fromString(s);
        return j.get<T>(); // relies on from_json overload
    }

    static void toFile(const json& j, const std::filesystem::path& path, int indent = 2) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path, std::ios::binary);
        ofs << j.dump(indent);
    }

    static json fromFile(const std::filesystem::path& path) {
        std::ifstream ifs(path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return fromString(content);
    }
};

} // namespace axonvex::utils::serialization
