/**
 * @file fileManager.hpp
 * @brief File Management for AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <fstream>

namespace axonvex::io::files {

/**
 * @brief High-performance file operations manager
 */
class FileManager {
public:
    // File existence and properties
    static bool exists(const std::string& path);
    static bool isFile(const std::string& path);
    static bool isDirectory(const std::string& path);
    static size_t getFileSize(const std::string& path);
    static std::string getFileExtension(const std::string& path);

    // File operations
    static std::vector<uint8_t> readBinary(const std::string& path);
    static std::string readText(const std::string& path);
    static bool writeBinary(const std::string& path, const std::vector<uint8_t>& data);
    static bool writeText(const std::string& path, const std::string& content);
    static bool appendText(const std::string& path, const std::string& content);

    // Directory operations
    static std::vector<std::string> listFiles(const std::string& directory);
    static std::vector<std::string> listDirectories(const std::string& directory);
    static bool createDirectory(const std::string& path);
    static bool removeDirectory(const std::string& path);
    static bool createDirectories(const std::string& path);

    // File watching (basic implementation)
    using FileChangeCallback = std::function<void(const std::string&, const std::string&)>;
    static void watchFile(const std::string& path, FileChangeCallback callback);
    static void unwatchFile(const std::string& path);

    // Utility methods
    static std::string getAbsolutePath(const std::string& path);
    static std::string getRelativePath(const std::string& path, const std::string& base);
    static bool copyFile(const std::string& source, const std::string& destination);
    static bool moveFile(const std::string& source, const std::string& destination);
    static bool deleteFile(const std::string& path);

private:
    static bool initializeFileSystem();
    static void cleanupFileSystem();
};

/**
 * @brief RAII file handle wrapper
 */
class FileHandle {
public:
    explicit FileHandle(const std::string& path, const std::string& mode = "r");
    ~FileHandle();

    bool isOpen() const;
    bool read(void* buffer, size_t size, size_t& bytesRead);
    bool write(const void* buffer, size_t size);
    bool seek(long offset, int origin = SEEK_SET);
    long tell() const;
    bool flush();

private:
    std::FILE* file_;
    std::string path_;
    bool isOpen_;
};

} // namespace axonvex::io::files