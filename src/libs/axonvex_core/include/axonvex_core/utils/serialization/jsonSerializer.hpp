#pragma once

#include <axonvex_core/detail/filesystem_compat.hpp>
#include <axonvex_core/utils/optional.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

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

    static void toFile(const json& j, const axonvex_fs::path& path, int indent = 2) {
        axonvex_fs::create_directories(path.parent_path());
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) {
            throw std::runtime_error("JSONSerializer: cannot open file for writing: " +
                                     path.string());
        }
        ofs << j.dump(indent);
        if (!ofs.good()) {
            throw std::runtime_error("JSONSerializer: failed to write JSON to file: " +
                                     path.string());
        }
    }

    static json fromFile(const axonvex_fs::path& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            throw std::runtime_error("JSONSerializer: cannot open file for reading: " +
                                     path.string());
        }
        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        return fromString(content);
    }

    // Non-throwing variants
    static bool toFileNoThrow(const json& j, const axonvex_fs::path& path, int indent = 2) {
        try {
            toFile(j, path, indent);
            return true;
        } catch (...) { return false; }
    }

    static axonvex::optional<json> fromFileNoThrow(const axonvex_fs::path& path) {
        try {
            return fromFile(path);
        } catch (...) { return axonvex::nullopt; }
    }

    template <typename T>
    static void serializeToFile(const T& obj, const axonvex_fs::path& path, int indent = 2) {
        json j = obj;
        toFile(j, path, indent);
    }

    template <typename T>
    static axonvex::optional<T> deserializeFromFileNoThrow(const axonvex_fs::path& path) {
        auto j = fromFileNoThrow(path);
        if (!j)
            return axonvex::nullopt;
        try {
            return j.value().template get<T>();
        } catch (...) { return axonvex::nullopt; }
    }
};

} // namespace axonvex::utils::serialization
