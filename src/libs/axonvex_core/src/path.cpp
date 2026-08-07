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

#include <algorithm>
#include <axonvex_core/path.hpp>
#include <cctype>
#include <chrono>
#include <fstream>
#include <random>
#include <regex>

#ifdef _WIN32
#include <shlobj.h>
#include <userenv.h>
#include <windows.h>
#pragma comment(lib, "userenv.lib")
#else
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

axonvex_fs::path pathRelativeCompat(const axonvex_fs::path& p, const axonvex_fs::path& base) {
    try {
        axonvex_fs::path c1 = axonvex_fs::canonical(p);
        axonvex_fs::path c2 = axonvex_fs::canonical(base);
        axonvex_fs::path result;
        auto it1 = c1.begin(), e1 = c1.end();
        auto it2 = c2.begin(), e2 = c2.end();
        while (it1 != e1 && it2 != e2 && *it1 == *it2) {
            ++it1;
            ++it2;
        }
        for (; it2 != e2; ++it2) {
            result /= "..";
        }
        for (; it1 != e1; ++it1) {
            result /= *it1;
        }
        return result;
    } catch (...) { return p; }
}

} // namespace

namespace axonvex::core {

// Static member definitions
std::unordered_map<Path::DefaultDir, axonvex_fs::path> Path::default_dirs_;
Path::SecurityLevel Path::default_security_level_ = Path::SecurityLevel::BASIC;
std::mutex Path::static_mutex_;

//==============================================================================
// Construction and Assignment
//==============================================================================

Path::Path(const std::string& path_str) : path_(path_str) {
    ensureInitialized();
}

Path::Path(const char* path_str) : path_(path_str) {
    ensureInitialized();
}

Path::Path(const axonvex_fs::path& fs_path) : path_(fs_path) {
    ensureInitialized();
}

//==============================================================================
// Static Initialization
//==============================================================================

void Path::ensureInitialized() {
    // C15(a): C++11 magic static — the standard guarantees exactly-once
    // execution of the lambda body and that every thread's return from
    // this declaration synchronizes-with that execution, so a caller that
    // returns from ensureInitialized() is guaranteed to see the writes
    // initializeDefaultDirs() made, without needing initialized_'s broken
    // outside-the-lock bool check. The lambda still takes static_mutex_
    // around the write: default_dirs_ also has post-init writers
    // (setDefaultDir, resetDefaultDirs, both under static_mutex_), and
    // this is the only writer of the three that would otherwise run
    // without that lock.
    static const bool once = []() {
        std::lock_guard<std::mutex> lock(static_mutex_);
        initializeDefaultDirs();
        return true;
    }();
    (void)once;
}

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
        default_dirs_[DefaultDir::TEMP] = axonvex_fs::temp_directory_path() / "AxonVex";
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
        default_dirs_[DefaultDir::TEMP] = axonvex_fs::temp_directory_path() / "axonvex";
        default_dirs_[DefaultDir::PLUGINS] = default_dirs_[DefaultDir::DATA] / "plugins";
        default_dirs_[DefaultDir::TEMPLATES] = default_dirs_[DefaultDir::DATA] / "templates";
        default_dirs_[DefaultDir::BACKUP] = default_dirs_[DefaultDir::DATA] / "backup";
        default_dirs_[DefaultDir::EXPORT] = default_dirs_[DefaultDir::DATA] / "export";
#endif

    } catch (const std::exception&) {
        // Fallback to current working directory
        auto cwd = getCurrentWorkingDirectory();
        for (auto& kv : default_dirs_) {
            kv.second = cwd / "axonvex";
        }
    }
}

axonvex_fs::path Path::getHomeDirectory() {
#ifdef _WIN32
    wchar_t* profile_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile_path))) {
        axonvex_fs::path result(profile_path);
        CoTaskMemFree(profile_path);
        return result;
    }

    // Fallback
    const char* home = std::getenv("USERPROFILE");
    if (home) {
        return axonvex_fs::path(home);
    }
    return axonvex_fs::path("C:\\");
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return axonvex_fs::path(home);
    }

    // Fallback using getpwuid
    struct passwd* pwd = getpwuid(getuid());
    if (pwd && pwd->pw_dir) {
        return axonvex_fs::path(pwd->pw_dir);
    }

    return axonvex_fs::path("/tmp");
#endif
}

axonvex_fs::path Path::getSystemAppDataDirectory() {
#ifdef _WIN32
    wchar_t* app_data_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &app_data_path))) {
        axonvex_fs::path result(app_data_path);
        CoTaskMemFree(app_data_path);
        return result;
    }

    // Fallback
    const char* app_data = std::getenv("APPDATA");
    if (app_data) {
        return axonvex_fs::path(app_data);
    }
    return getHomeDirectory();
#else
    return getHomeDirectory() / ".local" / "share";
#endif
}

axonvex_fs::path Path::getCurrentWorkingDirectory() {
    try {
        return axonvex_fs::current_path();
    } catch (const std::exception&) { return axonvex_fs::path("."); }
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
    ensureInitialized();

    std::lock_guard<std::mutex> lock(static_mutex_);
    auto it = default_dirs_.find(dir_type);
    if (it != default_dirs_.end()) {
        return Path(it->second);
    }

    // Fallback
    return Path(".");
}

void Path::setDefaultDir(DefaultDir dir_type, const Path& custom_path) {
    ensureInitialized();
    std::lock_guard<std::mutex> lock(static_mutex_);
    default_dirs_[dir_type] = custom_path.path_;
}

void Path::resetDefaultDirs() {
    ensureInitialized();
    std::lock_guard<std::mutex> lock(static_mutex_);
    default_dirs_.clear();
    initializeDefaultDirs();
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
        return Path(pathRelativeCompat(path_, base.path_));
    } catch (const std::exception&) { return *this; }
}

Path Path::absolute() const {
    try {
        return Path(axonvex_fs::absolute(path_));
    } catch (const std::exception&) { return *this; }
}

Path Path::canonical() const {
    try {
        return Path(axonvex_fs::canonical(path_));
    } catch (const std::exception&) { return absolute(); }
}

Path Path::normalize() const {
    try {
        return Path(axonvex_fs::canonical(path_));
    } catch (const std::exception&) {
        try {
            return Path(axonvex_fs::absolute(path_));
        } catch (const std::exception&) { return *this; }
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
        return axonvex_fs::exists(path_);
    } catch (const std::exception&) { return false; }
}

bool Path::isFile() const {
    try {
        return axonvex_fs::is_regular_file(path_);
    } catch (const std::exception&) { return false; }
}

bool Path::isDirectory() const {
    try {
        return axonvex_fs::is_directory(path_);
    } catch (const std::exception&) { return false; }
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
            return axonvex_fs::file_size(path_);
        }
        return 0;
    } catch (const std::exception&) { return 0; }
}

axonvex_fs::file_time_type Path::lastWriteTime() const {
    try {
        return axonvex_fs::last_write_time(path_);
    } catch (const std::exception&) { return axonvex_fs::file_time_type{}; }
}

bool Path::isReadable() const {
    try {
        // Try to open for reading
        if (isFile()) {
            std::ifstream file(path_, std::ios::binary);
            return file.good();
        } else if (isDirectory()) {
            // Try to list directory
            axonvex_fs::directory_iterator it(path_);
            return true;
        }
        return false;
    } catch (const std::exception&) { return false; }
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
                auto temp_path =
                    path_ /
                    ("temp_write_test_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::ofstream file(temp_path);
                if (file.good()) {
                    file.close();
                    axonvex_fs::remove(temp_path);
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
    } catch (const std::exception&) { return false; }
}

//==============================================================================
// Directory Operations
//==============================================================================

bool Path::createDirectory(bool recursive) const {
    try {
        if (recursive) {
            return axonvex_fs::create_directories(path_);
        } else {
            return axonvex_fs::create_directory(path_);
        }
    } catch (const std::exception&) { return false; }
}

bool Path::createDirectories() const {
    return createDirectory(true);
}

bool Path::remove() const {
    try {
        return axonvex_fs::remove(path_);
    } catch (const std::exception&) { return false; }
}

std::uintmax_t Path::removeAll() const {
    try {
        return axonvex_fs::remove_all(path_);
    } catch (const std::exception&) { return 0; }
}

std::vector<Path> Path::listDirectory(bool recursive) const {
    std::vector<Path> result;

    try {
        if (!isDirectory()) {
            return result;
        }

        if (recursive) {
            for (const auto& entry : axonvex_fs::recursive_directory_iterator(path_)) {
                result.emplace_back(entry.path());
            }
        } else {
            for (const auto& entry : axonvex_fs::directory_iterator(path_)) {
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

        axonvex_fs::copy_file(path_, destination.path_,
                              overwrite ? axonvex_fs::copy_options::overwrite_existing
                                        : axonvex_fs::copy_options::none);
        return true;
    } catch (const std::exception&) { return false; }
}

bool Path::moveTo(const Path& destination) const {
    try {
        // Create parent directories if needed
        destination.parent().createDirectories();

        axonvex_fs::rename(path_, destination.path_);
        return true;
    } catch (const std::exception&) { return false; }
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

const axonvex_fs::path& Path::native() const noexcept {
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

//==============================================================================
// Integration with AxonVex Components
//==============================================================================

Path Path::createConfigPath(const std::string& config_name, const std::string& subdir) {
    Path config_dir = getDefaultConfigDir();

    if (!subdir.empty()) {
        config_dir /= subdir;
    }

    // Ensure .json extension
    // C15(b): filename.length() - 5 underflowed size_t for names shorter
    // than the extension (e.g. "a"), so substr() threw std::out_of_range.
    std::string filename = config_name;
    if (filename.length() < 5 || filename.substr(filename.length() - 5) != ".json") {
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
    // C15(b): same underflow as createConfigPath, with the 4-char ".log".
    std::string filename = log_name;
    if (filename.length() < 4 || filename.substr(filename.length() - 4) != ".log") {
        filename += ".log";
    }

    return log_dir / filename;
}

Path Path::createTempPath(const std::string& prefix, const std::string& extension) {
    Path temp_dir = getDefaultTempDir();

    // Generate unique filename
    auto now = std::chrono::steady_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::string filename =
        prefix + std::to_string(timestamp) + "_" + std::to_string(dis(gen)) + extension;

    return temp_dir / filename;
}

//==============================================================================
// Private Helper Methods
//==============================================================================

bool Path::hasDirectoryTraversal() const {
    // C15(c): component-based check, not substring. The old
    // find("..")/find("./")/find(".\\") scan flagged legitimate names that
    // merely contain those characters ("my..file.json", "./config/x.json")
    // while being no harder to construct around. Traversal iff a path
    // component is exactly "..". Split manually on both '/' and '\\'
    // (rather than relying on axonvex_fs::path's own iteration, which is
    // native-separator-only and would miss a Windows-separator escape
    // attempt on a POSIX build) so this is a trust boundary that holds
    // regardless of platform or which separator the caller used.
    const std::string path_str = toString();
    std::string component;
    for (std::size_t i = 0; i <= path_str.size(); ++i) {
        if (i == path_str.size() || path_str[i] == '/' || path_str[i] == '\\') {
            if (component == "..") {
                return true;
            }
            component.clear();
        } else {
            component += path_str[i];
        }
    }
    return false;
}

bool Path::isInWhitelist() const {
    // For strict security, check if path is within allowed directories
    try {
        auto canonical_path = canonical();

        // ponytail: canonical() falls back to absolute() when the target doesn't
        // exist yet (common case for createConfigPath/createLogPath outputs), so
        // at STRICT level the prefix check is not symlink-resolved for to-be-created
        // files — a symlinked component inside a whitelisted dir can escape it.
        // C15(d): default_dirs_ has post-init writers (setDefaultDir(),
        // resetDefaultDirs()), both under static_mutex_ — grep confirms
        // they are the only ones — so this read must take the same lock;
        // iterating unlocked raced their mutation of the map.
        std::lock_guard<std::mutex> lock(static_mutex_);

        // Check if path is within any of the default directories
        for (const auto& kv : default_dirs_) {
            try {
                const axonvex_fs::path& dir_path = kv.second;
                auto canonical_dir = axonvex_fs::canonical(dir_path);
                auto relative = pathRelativeCompat(canonical_path.path_, canonical_dir);

                // If relative path doesn't start with "..", it's within the directory
                if (!relative.empty() && relative.begin()->string() != "..") {
                    return true;
                }
            } catch (const std::exception&) { continue; }
        }

        return false;
    } catch (const std::exception&) { return false; }
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
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

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
