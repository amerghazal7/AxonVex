#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <filesystem>
#include <optional>

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
        if (!ofs) {
            throw std::runtime_error("JSONSerializer: cannot open file for writing: " + path.string());
        }
        ofs << j.dump(indent);
        if (!ofs.good()) {
            throw std::runtime_error("JSONSerializer: failed to write JSON to file: " + path.string());
        }
    }

    static json fromFile(const std::filesystem::path& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            throw std::runtime_error("JSONSerializer: cannot open file for reading: " + path.string());
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return fromString(content);
    }

    // Non-throwing variants
    static bool toFileNoThrow(const json& j, const std::filesystem::path& path, int indent = 2) {
        try { toFile(j, path, indent); return true; } catch (...) { return false; }
    }

    static std::optional<json> fromFileNoThrow(const std::filesystem::path& path) {
        try { return fromFile(path); } catch (...) { return std::nullopt; }
    }

    template <typename T>
    static void serializeToFile(const T& obj, const std::filesystem::path& path, int indent = 2) {
        json j = obj;
        toFile(j, path, indent);
    }

    template <typename T>
    static std::optional<T> deserializeFromFileNoThrow(const std::filesystem::path& path) {
        auto j = fromFileNoThrow(path);
        if (!j) return std::nullopt;
        try { return j->template get<T>(); } catch (...) { return std::nullopt; }
    }
};

} // namespace axonvex::utils::serialization
