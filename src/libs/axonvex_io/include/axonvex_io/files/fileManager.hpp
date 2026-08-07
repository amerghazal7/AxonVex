#pragma once

#include <axonvex_core/detail/filesystem_compat.hpp>
#include <fstream>
#include <string>
#include <vector>

namespace axonvex::io::files {

class FileManager {
  public:
    static bool exists(const std::string& path) {
        return axonvex_fs::exists(path);
    }
    static bool isFile(const std::string& path) {
        return axonvex_fs::is_regular_file(path);
    }
    static bool isDirectory(const std::string& path) {
        return axonvex_fs::is_directory(path);
    }

    static std::string readText(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open())
            return {};
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    static bool writeText(const std::string& path, const std::string& content) {
        std::ofstream ofs(path);
        if (!ofs.is_open())
            return false;
        ofs << content;
        return true;
    }

    static std::vector<std::string> listFiles(const std::string& directory) {
        std::vector<std::string> result;
        for (auto& p : axonvex_fs::directory_iterator(directory)) {
            if (axonvex_fs::is_regular_file(p.status()))
                result.push_back(p.path().string());
        }
        return result;
    }

    static std::vector<std::string> listDirectories(const std::string& directory) {
        std::vector<std::string> result;
        for (auto& p : axonvex_fs::directory_iterator(directory)) {
            if (axonvex_fs::is_directory(p.status()))
                result.push_back(p.path().string());
        }
        return result;
    }
};

} // namespace axonvex::io::files
