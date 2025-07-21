/**
 * @file path.cpp
 * @brief Cross-platform Path Management Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 * 
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 * 
 * This file implements the comprehensive Path class for cross-platform
 * file and directory path management with security, validation, and
 * integration with AxonVex framework components.
 */

#include <axonvex/core/path.hpp>
#include <algorithm>
#include <random>
#include <chrono>
#include <regex>
#include <cctype>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <userenv.h>
    #pragma comment(lib, "userenv.lib")
#else
    #include <unistd.h>
    #include <pwd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

namespace axonvex::core {

// Static member definitions
std::unordered_map<Path::DefaultDir, std::filesystem::path> Path::default_dirs_;
Path::SecurityLevel Path::default_security_level_ = Path::SecurityLevel::BASIC;
bool Path::initialized_ = false;
std::mutex Path::static_mutex_;
std::unordered_map<std::string, std::function<bool(const Path&)>> Path::custom_validators_;

//==============================================================================
// Construction and Assignment
//==============================================================================

Path::Path(const std::string& path_str) : path_(path_str) {
    if (!initialized_) {
        std::lock_guard<std::mutex> lock(static_mutex_);
        if (!initialized_) {
            initializeDefaultDirs();
            initialized_ = true;
        }
    }
}

Path::Path(const char* path_str) : path_(path_str) {
    if (!initialized_) {
        std::lock_guard<std::mutex> lock(static_mutex_);
        if (!initialized_) {
            initializeDefaultDirs();
            initialized_ = true;
        }
    }
}

Path::Path(const std::filesystem::path& fs_path) : path_(fs_path) {
    if (!initialized_) {
        std::lock_guard<std::mutex> lock(static_mutex_);
        if (!initialized_) {
            initializeDefaultDirs();
            initialized_ = true;
        }
    }
}

//==============================================================================
// Static Initialization
//==============================================================================

void Path::initializeDefaultDirs() {
    try {
        // Get system directories
        auto home_dir = getHomeDirectory();
        auto app_data_dir = getSystemAppDataDirectory();
        auto cwd = getCurrentWorkingDirectory();
        
        // Set default directories with cross-platform logic
#ifdef _WIN32
        // Windows paths
        default_dirs_[DefaultDir::APP] = app_data_dir / "AxonVex";
        default_dirs_[DefaultDir::CONFIG] = default_dirs_[DefaultDir::APP] / "config";
        default_dirs_[DefaultDir::LOG] = default_dirs_[DefaultDir::APP] / "logs";
        default_dirs_[DefaultDir::CACHE] = default_dirs_[DefaultDir::APP] / "cache";
        default_dirs_[DefaultDir::DATA] = default_dirs_[DefaultDir::APP] / "data";
        default_dirs_[DefaultDir::TEMP] = std::filesystem::temp_directory_path() / "AxonVex";
        default_dirs_[DefaultDir::PLUGINS] = default_dirs_[DefaultDir::APP] / "plugins";
        default_dirs_[DefaultDir::TEMPLATES] = default_dirs_[DefaultDir::APP] / "templates";
        default_dirs_[DefaultDir::BACKUP] = default_dirs_[DefaultDir::APP] / "backup";
        default_dirs_[DefaultDir::EXPORT] = default_dirs_[DefaultDir::APP] / "export";
#else
        // Unix-like paths (Linux, macOS)
        default_dirs_[DefaultDir::APP] = home_dir / ".axonvex";
        default_dirs_[DefaultDir::CONFIG] = home_dir / ".config" / "axonvex";
        default_dirs_[DefaultDir::LOG] = home_dir / ".local" / "share" / "axonvex" / "logs";
        default_dirs_[DefaultDir::CACHE] = home_dir / ".cache" / "axonvex";
        default_dirs_[DefaultDir::DATA] = home_dir / ".local" / "share" / "axonvex";
        default_dirs_[DefaultDir::TEMP] = std::filesystem::temp_directory_path() / "axonvex";
        default_dirs_[DefaultDir::PLUGINS] = default_dirs_[DefaultDir::DATA] / "plugins";
        default_dirs_[DefaultDir::TEMPLATES] = default_dirs_[DefaultDir::DATA] / "templates";
        default_dirs_[DefaultDir::BACKUP] = default_dirs_[DefaultDir::DATA] / "backup";
        default_dirs_[DefaultDir::EXPORT] = default_dirs_[DefaultDir::DATA] / "export";
#endif
        
    } catch (const std::exception&) {
        // Fallback to current working directory
        auto cwd = getCurrentWorkingDirectory();
        for (auto& [dir_type, path] : default_dirs_) {
            default_dirs_[dir_type] = cwd / "axonvex";
        }
    }
}

std::filesystem::path Path::getHomeDirectory() {
#ifdef _WIN32
    wchar_t* profile_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile_path))) {
        std::filesystem::path result(profile_path);
        CoTaskMemFree(profile_path);
        return result;
    }
    
    // Fallback
    const char* home = std::getenv("USERPROFILE");
    if (home) {
        return std::filesystem::path(home);
    }
    return std::filesystem::path("C:\\");
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home);
    }
    
    // Fallback using getpwuid
    struct passwd* pwd = getpwuid(getuid());
    if (pwd && pwd->pw_dir) {
        return std::filesystem::path(pwd->pw_dir);
    }
    
    return std::filesystem::path("/tmp");
#endif
}

std::filesystem::path Path::getSystemAppDataDirectory() {
#ifdef _WIN32
    wchar_t* app_data_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &app_data_path))) {
        std::filesystem::path result(app_data_path);
        CoTaskMemFree(app_data_path);
        return result;
    }
    
    // Fallback
    const char* app_data = std::getenv("APPDATA");
    if (app_data) {
        return std::filesystem::path(app_data);
    }
    return getHomeDirectory();
#else
    return getHomeDirectory() / ".local" / "share";
#endif
}

std::filesystem::path Path::getCurrentWorkingDirectory() {
    try {
        return std::filesystem::current_path();
    } catch (const std::exception&) {
        return std::filesystem::path(".");
    }
}

//==============================================================================
// Default Directory Management
//==============================================================================

Path Path::getDefaultAppDir() {
    return Path(getDefaultDir(DefaultDir::APP));
}

Path Path::getDefaultConfigDir() {
    return Path(getDefaultDir(DefaultDir::CONFIG));
}

Path Path::getDefaultLogDir() {
    return Path(getDefaultDir(DefaultDir::LOG));
}

Path Path::getDefaultCacheDir() {
    return Path(getDefaultDir(DefaultDir::CACHE));
}

Path Path::getDefaultDataDir() {
    return Path(getDefaultDir(DefaultDir::DATA));
}

Path Path::getDefaultTempDir() {
    return Path(getDefaultDir(DefaultDir::TEMP));
}

Path Path::getDefaultDir(DefaultDir dir_type) {
    std::lock_guard<std::mutex> lock(static_mutex_);
    if (!initialized_) {
        initializeDefaultDirs();
        initialized_ = true;
    }
    
    auto it = default_dirs_.find(dir_type);
    if (it != default_dirs_.end()) {
        return Path(it->second);
    }
    
    // Fallback
    return Path(".");
}

void Path::setDefaultDir(DefaultDir dir_type, const Path& custom_path) {
    std::lock_guard<std::mutex> lock(static_mutex_);
    default_dirs_[dir_type] = custom_path.path_;
}

void Path::resetDefaultDirs() {
    std::lock_guard<std::mutex> lock(static_mutex_);
    default_dirs_.clear();
    initialized_ = false;
    initializeDefaultDirs();
    initialized_ = true;
}

//==============================================================================
// Path Composition and Manipulation
//==============================================================================

Path Path::operator/(const Path& other) const {
    return Path(path_ / other.path_);
}

Path Path::operator/(const std::string& component) const {
    return Path(path_ / component);
}

Path Path::operator/(const char* component) const {
    return Path(path_ / component);
}

Path& Path::operator/=(const Path& component) {
    path_ /= component.path_;
    string_cache_valid_ = false;
    return *this;
}

Path& Path::operator/=(const std::string& component) {
    path_ /= component;
    string_cache_valid_ = false;
    return *this;
}

Path Path::parent() const {
    return Path(path_.parent_path());
}

std::string Path::filename() const {
    return path_.filename().string();
}

std::string Path::extension() const {
    return path_.extension().string();
}

std::string Path::stem() const {
    return path_.stem().string();
}

Path Path::relativeTo(const Path& base) const {
    try {
        return Path(std::filesystem::relative(path_, base.path_));
    } catch (const std::exception&) {
        return *this;
    }
}

Path Path::absolute() const {
    try {
        return Path(std::filesystem::absolute(path_));
    } catch (const std::exception&) {
        return *this;
    }
}

Path Path::canonical() const {
    try {
        return Path(std::filesystem::canonical(path_));
    } catch (const std::exception&) {
        return absolute();
    }
}

Path Path::normalize() const {
    try {
        auto normalized = path_;
        normalized = normalized.lexically_normal();
        return Path(normalized);
    } catch (const std::exception&) {
        return *this;
    }
}

Path Path::replaceExtension(const std::string& new_extension) const {
    auto result = path_;
    result.replace_extension(new_extension);
    return Path(result);
}

Path Path::replaceFilename(const std::string& new_filename) const {
    auto result = path_;
    result.replace_filename(new_filename);
    return Path(result);
}

//==============================================================================
// Path Properties and Validation
//==============================================================================

bool Path::exists() const {
    try {
        return std::filesystem::exists(path_);
    } catch (const std::exception&) {
        return false;
    }
}

bool Path::isFile() const {
    try {
        return std::filesystem::is_regular_file(path_);
    } catch (const std::exception&) {
        return false;
    }
}

bool Path::isDirectory() const {
    try {
        return std::filesystem::is_directory(path_);
    } catch (const std::exception&) {
        return false;
    }
}

bool Path::isAbsolute() const {
    return path_.is_absolute();
}

bool Path::isRelative() const {
    return path_.is_relative();
}

bool Path::empty() const {
    return path_.empty();
}

bool Path::isSecure(SecurityLevel level) const {
    auto errors = validateSecurity(level);
    return errors.empty();
}

std::vector<std::string> Path::validateSecurity(SecurityLevel level) const {
    std::vector<std::string> errors;
    
    if (level == SecurityLevel::NONE) {
        return errors;
    }
    
    // Basic security checks
    if (hasDirectoryTraversal()) {
        errors.push_back("Path contains directory traversal sequences (.. or .\\.. or /../)");
    }
    
    if (hasIllegalCharacters()) {
        errors.push_back("Path contains illegal characters");
    }
    
    if (exceedsPathLimits()) {
        errors.push_back("Path exceeds system length limits");
    }
    
    // Additional checks for higher security levels
    if (level >= SecurityLevel::STRICT) {
        if (!isInWhitelist()) {
            errors.push_back("Path is not in allowed directory whitelist");
        }
    }
    
    if (level == SecurityLevel::PARANOID) {
        // Additional paranoid checks
        std::string path_str = toString();
        
        // Check for null bytes
        if (path_str.find('\0') != std::string::npos) {
            errors.push_back("Path contains null bytes");
        }
        
        // Check for control characters
        for (char c : path_str) {
            if (std::iscntrl(c) && c != '\t' && c != '\n' && c != '\r') {
                errors.push_back("Path contains control characters");
                break;
            }
        }
    }
    
    return errors;
}

std::uintmax_t Path::size() const {
    try {
        if (isFile()) {
            return std::filesystem::file_size(path_);
        }
        return 0;
    } catch (const std::exception&) {
        return 0;
    }
}

std::filesystem::file_time_type Path::lastWriteTime() const {
    try {
        return std::filesystem::last_write_time(path_);
    } catch (const std::exception&) {
        return std::filesystem::file_time_type{};
    }
}

bool Path::isReadable() const {
    try {
        // Try to open for reading
        if (isFile()) {
            std::ifstream file(path_, std::ios::binary);
            return file.good();
        } else if (isDirectory()) {
            // Try to list directory
            std::filesystem::directory_iterator it(path_);
            return true;
        }
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

bool Path::isWritable() const {
    try {
        if (exists()) {
            if (isFile()) {
                // Try to open for writing (append mode to not truncate)
                std::ofstream file(path_, std::ios::app);
                return file.good();
            } else if (isDirectory()) {
                // Try to create a temporary file in the directory
                auto temp_path = path_ / ("temp_write_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::ofstream file(temp_path);
                if (file.good()) {
                    file.close();
                    std::filesystem::remove(temp_path);
                    return true;
                }
                return false;
            }
        } else {
            // Check if parent directory is writable
            auto parent_path = parent();
            if (parent_path.exists() && parent_path.isDirectory()) {
                return parent_path.isWritable();
            }
        }
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

//==============================================================================
// Directory Operations
//==============================================================================

bool Path::createDirectory(bool recursive) const {
    try {
        if (recursive) {
            return std::filesystem::create_directories(path_);
        } else {
            return std::filesystem::create_directory(path_);
        }
    } catch (const std::exception&) {
        return false;
    }
}

bool Path::createDirectories() const {
    return createDirectory(true);
}

bool Path::remove() const {
    try {
        return std::filesystem::remove(path_);
    } catch (const std::exception&) {
        return false;
    }
}

std::uintmax_t Path::removeAll() const {
    try {
        return std::filesystem::remove_all(path_);
    } catch (const std::exception&) {
        return 0;
    }
}

std::vector<Path> Path::listDirectory(bool recursive) const {
    std::vector<Path> result;
    
    try {
        if (!isDirectory()) {
            return result;
        }
        
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path_)) {
                result.emplace_back(entry.path());
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(path_)) {
                result.emplace_back(entry.path());
            }
        }
    } catch (const std::exception&) {
        // Return empty vector on error
    }
    
    return result;
}

std::vector<Path> Path::findFiles(const std::string& pattern, bool recursive) const {
    std::vector<Path> result;
    
    try {
        if (!isDirectory()) {
            return result;
        }
        
        // Convert glob pattern to regex
        std::string regex_pattern = pattern;
        std::replace(regex_pattern.begin(), regex_pattern.end(), '*', '.');
        regex_pattern = ".*" + regex_pattern + ".*";
        std::regex pattern_regex(regex_pattern, std::regex_constants::icase);
        
        auto files = listDirectory(recursive);
        for (const auto& file : files) {
            if (file.isFile() && std::regex_match(file.filename(), pattern_regex)) {
                result.push_back(file);
            }
        }
    } catch (const std::exception&) {
        // Return empty vector on error
    }
    
    return result;
}

//==============================================================================
// File Operations
//==============================================================================

bool Path::copyTo(const Path& destination, bool overwrite) const {
    try {
        if (!overwrite && destination.exists()) {
            return false;
        }
        
        // Create parent directories if needed
        destination.parent().createDirectories();
        
        std::filesystem::copy_file(path_, destination.path_, 
            overwrite ? std::filesystem::copy_options::overwrite_existing : 
                       std::filesystem::copy_options::none);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Path::moveTo(const Path& destination) const {
    try {
        // Create parent directories if needed
        destination.parent().createDirectories();
        
        std::filesystem::rename(path_, destination.path_);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Path Path::createBackup(const std::string& backup_suffix) const {
    auto backup_path = Path(toString() + backup_suffix);
    
    if (copyTo(backup_path, true)) {
        return backup_path;
    }
    
    return Path(); // Return empty path on failure
}

Path Path::getUniqueFilename() const {
    if (!exists()) {
        return *this;
    }
    
    auto parent_dir = parent();
    auto file_stem = stem();
    auto file_ext = extension();
    
    int counter = 1;
    Path unique_path;
    
    do {
        std::string unique_name = file_stem + "_" + std::to_string(counter) + file_ext;
        unique_path = parent_dir / unique_name;
        counter++;
    } while (unique_path.exists() && counter < 10000);
    
    return unique_path;
}

//==============================================================================
// String Conversion and Comparison
//==============================================================================

std::string Path::toString() const {
    if (!string_cache_valid_) {
        cached_string_ = path_.string();
        string_cache_valid_ = true;
    }
    return cached_string_.value();
}

std::string Path::toGenericString() const {
    return path_.generic_string();
}

const char* Path::c_str() const {
    toString(); // Ensure cache is valid
    return cached_string_->c_str();
}

const std::filesystem::path& Path::native() const noexcept {
    return path_;
}

bool Path::operator==(const Path& other) const {
    return path_ == other.path_;
}

bool Path::operator!=(const Path& other) const {
    return path_ != other.path_;
}

bool Path::operator<(const Path& other) const {
    return path_ < other.path_;
}

Path::operator std::string() const {
    return toString();
}

//==============================================================================
// Utility Functions
//==============================================================================

void Path::setDefaultSecurityLevel(SecurityLevel level) {
    std::lock_guard<std::mutex> lock(static_mutex_);
    default_security_level_ = level;
}

Path::SecurityLevel Path::getDefaultSecurityLevel() {
    std::lock_guard<std::mutex> lock(static_mutex_);
    return default_security_level_;
}

void Path::registerValidator(const std::string& name, std::function<bool(const Path&)> validator) {
    std::lock_guard<std::mutex> lock(static_mutex_);
    custom_validators_[name] = std::move(validator);
}

bool Path::validateWith(const std::string& validator_name) const {
    std::lock_guard<std::mutex> lock(static_mutex_);
    auto it = custom_validators_.find(validator_name);
    if (it != custom_validators_.end()) {
        return it->second(*this);
    }
    return false;
}

std::unordered_map<std::string, std::string> Path::getSystemInfo() {
    std::unordered_map<std::string, std::string> info;
    
    try {
        info["current_path"] = std::filesystem::current_path().string();
        info["temp_directory"] = std::filesystem::temp_directory_path().string();
        
        // Path limits (platform-specific)
#ifdef _WIN32
        info["max_path_length"] = "260"; // Traditional limit, 32767 with long path support
        info["separator"] = "\\";
        info["case_sensitive"] = "false";
#else
        info["max_path_length"] = "4096"; // Typical Linux limit
        info["separator"] = "/";
        info["case_sensitive"] = "true";
#endif
        
        info["filesystem_space"] = std::to_string(std::filesystem::space(".").available);
        
    } catch (const std::exception& e) {
        info["error"] = e.what();
    }
    
    return info;
}

//==============================================================================
// Integration with AxonVex Components
//==============================================================================

Path Path::createConfigPath(const std::string& config_name, const std::string& subdir) {
    Path config_dir = getDefaultConfigDir();
    
    if (!subdir.empty()) {
        config_dir /= subdir;
    }
    
    // Ensure .json extension
    std::string filename = config_name;
    if (filename.substr(filename.length() - 5) != ".json") {
        filename += ".json";
    }
    
    return config_dir / filename;
}

Path Path::createLogPath(const std::string& log_name, const std::string& subdir) {
    Path log_dir = getDefaultLogDir();
    
    if (!subdir.empty()) {
        log_dir /= subdir;
    }
    
    // Ensure .log extension
    std::string filename = log_name;
    if (filename.substr(filename.length() - 4) != ".log") {
        filename += ".log";
    }
    
    return log_dir / filename;
}

Path Path::createTempPath(const std::string& prefix, const std::string& extension) {
    Path temp_dir = getDefaultTempDir();
    
    // Generate unique filename
    auto now = std::chrono::steady_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::string filename = prefix + std::to_string(timestamp) + "_" + std::to_string(dis(gen)) + extension;
    
    return temp_dir / filename;
}

//==============================================================================
// Private Helper Methods
//==============================================================================

bool Path::hasDirectoryTraversal() const {
    std::string path_str = toString();
    
    // Check for common directory traversal patterns
    if (path_str.find("..") != std::string::npos ||
        path_str.find("./") != std::string::npos ||
        path_str.find(".\\") != std::string::npos) {
        return true;
    }
    
    return false;
}

bool Path::isInWhitelist() const {
    // For strict security, check if path is within allowed directories
    try {
        auto canonical_path = canonical();
        
        // Check if path is within any of the default directories
        for (const auto& [dir_type, dir_path] : default_dirs_) {
            try {
                auto canonical_dir = std::filesystem::canonical(dir_path);
                auto relative = std::filesystem::relative(canonical_path.path_, canonical_dir);
                
                // If relative path doesn't start with "..", it's within the directory
                if (!relative.empty() && relative.begin()->string() != "..") {
                    return true;
                }
            } catch (const std::exception&) {
                continue;
            }
        }
        
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

bool Path::hasIllegalCharacters() const {
    std::string path_str = toString();
    
    // Platform-specific illegal characters
#ifdef _WIN32
    const std::string illegal_chars = "<>:\"|?*";
    for (char c : illegal_chars) {
        if (path_str.find(c) != std::string::npos) {
            return true;
        }
    }
    
    // Check for reserved names
    const std::vector<std::string> reserved_names = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    
    for (const auto& reserved : reserved_names) {
        if (path_str.find(reserved) != std::string::npos) {
            return true;
        }
    }
#else
    // Unix-like systems - mainly just null bytes are problematic
    if (path_str.find('\0') != std::string::npos) {
        return true;
    }
#endif
    
    return false;
}

bool Path::exceedsPathLimits() const {
    std::string path_str = toString();
    
#ifdef _WIN32
    // Windows traditional limit is 260, but can be higher with long path support
    return path_str.length() > 32767;
#else
    // Unix-like systems typically have a limit around 4096
    return path_str.length() > 4096;
#endif
}

//==============================================================================
// Global Operators and Functions
//==============================================================================

std::ostream& operator<<(std::ostream& os, const Path& path) {
    return os << path.toString();
}

Path operator/(const std::string& lhs, const Path& rhs) {
    return Path(lhs) / rhs;
}

Path operator/(const char* lhs, const Path& rhs) {
    return Path(lhs) / rhs;
}

} // namespace axonvex::core 