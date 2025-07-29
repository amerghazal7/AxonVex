#include <axonvex/io/files/fileManager.hpp>
#include <axonvex/core/logger.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace axonvex::io::files {

bool FileManager::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool FileManager::isFile(const std::string& path) {
    return std::filesystem::is_regular_file(path);
}

bool FileManager::isDirectory(const std::string& path) {
    return std::filesystem::is_directory(path);
}

size_t FileManager::getFileSize(const std::string& path) {
    if (!exists(path) || !isFile(path)) {
        return 0;
    }
    return std::filesystem::file_size(path);
}

std::string FileManager::getFileExtension(const std::string& path) {
    std::filesystem::path p(path);
    return p.extension().string();
}

std::vector<uint8_t> FileManager::readBinary(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        axonvex::core::Logger logger;
        logger.error("FileManager", "Failed to open file for binary reading: " + path);
        return {};
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        axonvex::core::Logger logger;
        logger.error("FileManager", "Failed to read binary data from file: " + path);
        return {};
    }
    
    return buffer;
}

std::string FileManager::readText(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        axonvex::core::Logger logger;
        logger.error("FileManager", "Failed to open file for text reading: " + path);
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    return content;
}

bool FileManager::writeBinary(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        axonvex::core::Logger logger;
        logger.error("FileManager", "Failed to open file for binary writing: " + path);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

bool FileManager::writeText(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        axonvex::core::Logger logger;
        logger.error("FileManager", "Failed to open file for text writing: " + path);
        return false;
    }
    
    file << content;
    return file.good();
}

bool FileManager::appendText(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::app);
    if (!file.is_open()) {
        axonvex::core::Logger logger;
        logger.error("FileManager", "Failed to open file for text appending: " + path);
        return false;
    }
    
    file << content;
    return file.good();
}

std::vector<std::string> FileManager::listFiles(const std::string& directory) {
    std::vector<std::string> files;
    
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return files;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    
    return files;
}

std::vector<std::string> FileManager::listDirectories(const std::string& directory) {
    std::vector<std::string> directories;
    
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return directories;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_directory()) {
            directories.push_back(entry.path().string());
        }
    }
    
    return directories;
}

bool FileManager::createDirectory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::create_directory(path, ec);
}

bool FileManager::createDirectories(const std::string& path) {
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

bool FileManager::removeDirectory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove_all(path, ec) > 0;
}

std::string FileManager::getAbsolutePath(const std::string& path) {
    return std::filesystem::absolute(path).string();
}

bool FileManager::copyFile(const std::string& source, const std::string& destination) {
    std::error_code ec;
    return std::filesystem::copy_file(source, destination, ec);
}

bool FileManager::deleteFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

// FileHandle implementation
FileHandle::FileHandle(const std::string& path, const std::string& mode) 
    : file_(nullptr), path_(path), isOpen_(false) {
    file_ = std::fopen(path.c_str(), mode.c_str());
    isOpen_ = (file_ != nullptr);
}

FileHandle::~FileHandle() {
    if (file_) {
        std::fclose(file_);
    }
}

bool FileHandle::isOpen() const {
    return isOpen_;
}

bool FileHandle::read(void* buffer, size_t size, size_t& bytesRead) {
    if (!file_) return false;
    
    bytesRead = std::fread(buffer, 1, size, file_);
    return !std::ferror(file_);
}

bool FileHandle::write(const void* buffer, size_t size) {
    if (!file_) return false;
    
    size_t written = std::fwrite(buffer, 1, size, file_);
    return written == size && !std::ferror(file_);
}

bool FileHandle::flush() {
    if (!file_) return false;
    return std::fflush(file_) == 0;
}

} // namespace axonvex::io::files