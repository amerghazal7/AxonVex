#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>
#include <iostream>

namespace axonvex::core {

/**
 * @brief Cross-platform path management for AxonVex Framework
 * 
 * The Path class provides a comprehensive solution for file and directory path
 * management with cross-platform compatibility, type safety, and integration
 * with AxonVex framework components.
 * 
 * Features:
 * - Cross-platform path handling (Windows, Linux, macOS)
 * - Type-safe path composition and manipulation
 * - Default directory management (app, config, logs, cache, etc.)
 * - Path validation and security checks
 * - Integration with Configuration and Logger classes
 * - Atomic file operations and safe path creation
 * 
 * Performance characteristics:
 * - Path operations: O(1) for most operations
 * - Path composition: Efficient string concatenation
 * - Filesystem operations: Delegated to std::filesystem
 * - Memory usage: Minimal overhead over std::filesystem::path
 * 
 * @example Basic usage:
 * @code
 * // Default directories
 * Path appDir = Path::getDefaultAppDir();
 * Path configDir = Path::getDefaultConfigDir();
 * 
 * // Path composition
 * Path configFile = configDir / "systems" / "main.json";
 * Path logFile = Path::getDefaultLogDir() / "app.log";
 * 
 * // Path operations
 * if (configFile.exists()) {
 *     auto parent = configFile.parent();
 *     auto filename = configFile.filename();
 *     auto extension = configFile.extension();
 * }
 * 
 * // Safe directory creation
 * configFile.parent().createDirectories();
 * 
 * // Security validation
 * if (configFile.isSecure()) {
 *     // Safe to use
 * }
 * @endcode
 */
class Path {
public:
    /**
     * @brief Default directory types for AxonVex applications
     */
    enum class DefaultDir {
        APP,          ///< Main application directory
        CONFIG,       ///< Configuration files directory
        LOG,          ///< Log files directory
        CACHE,        ///< Cache files directory
        DATA,         ///< Application data directory
        TEMP,         ///< Temporary files directory
        PLUGINS,      ///< Plugin files directory
        TEMPLATES,    ///< Template files directory
        BACKUP,       ///< Backup files directory
        EXPORT        ///< Export files directory
    };
    
    /**
     * @brief Path security validation levels
     */
    enum class SecurityLevel {
        NONE,         ///< No security validation
        BASIC,        ///< Basic path traversal protection
        STRICT,       ///< Strict validation with whitelist
        PARANOID      ///< Maximum security with extensive checks
    };
    
    /**
     * @brief File operation modes for atomic operations
     */
    enum class FileMode {
        READ_ONLY,
        WRITE_ONLY,
        READ_WRITE,
        APPEND,
        CREATE_NEW,
        TRUNCATE
    };

private:
    std::filesystem::path path_;
    static std::unordered_map<DefaultDir, std::filesystem::path> default_dirs_;
    static SecurityLevel default_security_level_;
    static bool initialized_;
    
    // Thread-safe initialization
    static void initializeDefaultDirs();
    static std::filesystem::path getHomeDirectory();
    static std::filesystem::path getSystemAppDataDirectory();
    static std::filesystem::path getCurrentWorkingDirectory();

public:
    //==========================================================================
    // Construction and Assignment
    //==========================================================================
    
    /**
     * @brief Default constructor - creates empty path
     */
    Path() = default;
    
    /**
     * @brief Construct from string path
     * 
     * @param path_str Path string (supports both / and \ separators)
     */
    explicit Path(const std::string& path_str);
    
    /**
     * @brief Construct from C-style string
     * 
     * @param path_str Path C-string
     */
    explicit Path(const char* path_str);
    
    /**
     * @brief Construct from std::filesystem::path
     * 
     * @param fs_path Filesystem path object
     */
    explicit Path(const std::filesystem::path& fs_path);
    
    /**
     * @brief Copy constructor
     */
    Path(const Path& other) = default;
    
    /**
     * @brief Move constructor
     */
    Path(Path&& other) noexcept = default;
    
    /**
     * @brief Copy assignment
     */
    Path& operator=(const Path& other) = default;
    
    /**
     * @brief Move assignment
     */
    Path& operator=(Path&& other) noexcept = default;
    
    /**
     * @brief Destructor
     */
    ~Path() = default;
    
    //==========================================================================
    // Default Directory Management
    //==========================================================================
    
    /**
     * @brief Get default application directory
     * 
     * @return Path to application directory
     */
    static Path getDefaultAppDir();
    
    /**
     * @brief Get default configuration directory
     * 
     * @return Path to configuration directory
     */
    static Path getDefaultConfigDir();
    
    /**
     * @brief Get default log directory
     * 
     * @return Path to log directory
     */
    static Path getDefaultLogDir();
    
    /**
     * @brief Get default cache directory
     * 
     * @return Path to cache directory
     */
    static Path getDefaultCacheDir();
    
    /**
     * @brief Get default data directory
     * 
     * @return Path to data directory
     */
    static Path getDefaultDataDir();
    
    /**
     * @brief Get default temporary directory
     * 
     * @return Path to temporary directory
     */
    static Path getDefaultTempDir();
    
    /**
     * @brief Get default directory by type
     * 
     * @param dir_type Type of default directory
     * @return Path to specified directory type
     */
    static Path getDefaultDir(DefaultDir dir_type);
    
    /**
     * @brief Set custom default directory
     * 
     * @param dir_type Type of directory to set
     * @param custom_path Custom path for this directory type
     */
    static void setDefaultDir(DefaultDir dir_type, const Path& custom_path);
    
    /**
     * @brief Reset default directories to system defaults
     */
    static void resetDefaultDirs();
    
    //==========================================================================
    // Path Composition and Manipulation
    //==========================================================================
    
    /**
     * @brief Join paths with proper separator
     * 
     * @param other Path component to append
     * @return New path with components joined
     */
    Path operator/(const Path& other) const;
    
    /**
     * @brief Join with string path component
     * 
     * @param component String component to append
     * @return New path with component joined
     */
    Path operator/(const std::string& component) const;
    
    /**
     * @brief Join with C-string component
     * 
     * @param component C-string component to append
     * @return New path with component joined
     */
    Path operator/(const char* component) const;
    
    /**
     * @brief Append component to this path (in-place)
     * 
     * @param component Component to append
     * @return Reference to this path
     */
    Path& operator/=(const Path& component);
    
    /**
     * @brief Append string component to this path (in-place)
     * 
     * @param component String component to append
     * @return Reference to this path
     */
    Path& operator/=(const std::string& component);
    
    /**
     * @brief Get parent directory
     * 
     * @return Path to parent directory
     */
    Path parent() const;
    
    /**
     * @brief Get filename (last component)
     * 
     * @return Filename as string
     */
    std::string filename() const;
    
    /**
     * @brief Get file extension
     * 
     * @return File extension including dot (e.g., ".json")
     */
    std::string extension() const;
    
    /**
     * @brief Get filename without extension
     * 
     * @return Filename stem (without extension)
     */
    std::string stem() const;
    
    /**
     * @brief Get path relative to base path
     * 
     * @param base Base path for relative calculation
     * @return Relative path from base to this path
     */
    Path relativeTo(const Path& base) const;
    
    /**
     * @brief Get absolute path
     * 
     * @return Absolute path
     */
    Path absolute() const;
    
    /**
     * @brief Get canonical path (resolved symlinks, normalized)
     * 
     * @return Canonical path
     */
    Path canonical() const;
    
    /**
     * @brief Normalize path (remove . and .. components)
     * 
     * @return Normalized path
     */
    Path normalize() const;
    
    /**
     * @brief Replace extension
     * 
     * @param new_extension New extension (with or without dot)
     * @return Path with new extension
     */
    Path replaceExtension(const std::string& new_extension) const;
    
    /**
     * @brief Replace filename
     * 
     * @param new_filename New filename
     * @return Path with new filename
     */
    Path replaceFilename(const std::string& new_filename) const;
    
    //==========================================================================
    // Path Properties and Validation
    //==========================================================================
    
    /**
     * @brief Check if path exists
     * 
     * @return true if path exists
     */
    bool exists() const;
    
    /**
     * @brief Check if path is a file
     * 
     * @return true if path is a regular file
     */
    bool isFile() const;
    
    /**
     * @brief Check if path is a directory
     * 
     * @return true if path is a directory
     */
    bool isDirectory() const;
    
    /**
     * @brief Check if path is absolute
     * 
     * @return true if path is absolute
     */
    bool isAbsolute() const;
    
    /**
     * @brief Check if path is relative
     * 
     * @return true if path is relative
     */
    bool isRelative() const;
    
    /**
     * @brief Check if path is empty
     * 
     * @return true if path is empty
     */
    bool empty() const;
    
    /**
     * @brief Check if path is secure (no directory traversal)
     * 
     * @param level Security validation level
     * @return true if path passes security validation
     */
    bool isSecure(SecurityLevel level = SecurityLevel::BASIC) const;
    
    /**
     * @brief Validate path against security rules
     * 
     * @param level Security validation level
     * @return Vector of validation errors (empty if valid)
     */
    std::vector<std::string> validateSecurity(SecurityLevel level = SecurityLevel::BASIC) const;
    
    /**
     * @brief Get file size in bytes
     * 
     * @return File size or 0 if not a file
     */
    std::uintmax_t size() const;
    
    /**
     * @brief Get last write time
     * 
     * @return Last write time as filesystem time
     */
    std::filesystem::file_time_type lastWriteTime() const;
    
    /**
     * @brief Check if file/directory is readable
     * 
     * @return true if readable
     */
    bool isReadable() const;
    
    /**
     * @brief Check if file/directory is writable
     * 
     * @return true if writable
     */
    bool isWritable() const;
    
    //==========================================================================
    // Directory Operations
    //==========================================================================
    
    /**
     * @brief Create directory (and parent directories if needed)
     * 
     * @param recursive Create parent directories if true
     * @return true if directory was created or already exists
     */
    bool createDirectory(bool recursive = true) const;
    
    /**
     * @brief Create all parent directories
     * 
     * @return true if directories were created or already exist
     */
    bool createDirectories() const;
    
    /**
     * @brief Remove file or empty directory
     * 
     * @return true if removed successfully
     */
    bool remove() const;
    
    /**
     * @brief Remove directory and all contents
     * 
     * @return Number of files/directories removed
     */
    std::uintmax_t removeAll() const;
    
    /**
     * @brief List directory contents
     * 
     * @param recursive Include subdirectories if true
     * @return Vector of paths in directory
     */
    std::vector<Path> listDirectory(bool recursive = false) const;
    
    /**
     * @brief Find files matching pattern
     * 
     * @param pattern Glob pattern (e.g., "*.json")
     * @param recursive Search subdirectories if true
     * @return Vector of matching paths
     */
    std::vector<Path> findFiles(const std::string& pattern, bool recursive = false) const;
    
    //==========================================================================
    // File Operations
    //==========================================================================
    
    /**
     * @brief Copy file to destination
     * 
     * @param destination Destination path
     * @param overwrite Overwrite if destination exists
     * @return true if copied successfully
     */
    bool copyTo(const Path& destination, bool overwrite = false) const;
    
    /**
     * @brief Move file to destination
     * 
     * @param destination Destination path
     * @return true if moved successfully
     */
    bool moveTo(const Path& destination) const;
    
    /**
     * @brief Create atomic backup of file
     * 
     * @param backup_suffix Suffix for backup file (default: ".bak")
     * @return Path to backup file
     */
    Path createBackup(const std::string& backup_suffix = ".bak") const;
    
    /**
     * @brief Get unique filename (append number if file exists)
     * 
     * @return Path with unique filename
     */
    Path getUniqueFilename() const;
    
    //==========================================================================
    // String Conversion and Comparison
    //==========================================================================
    
    /**
     * @brief Convert to string with native separators
     * 
     * @return Path as string
     */
    std::string toString() const;
    
    /**
     * @brief Convert to string with generic separators (forward slash)
     * 
     * @return Path as generic string
     */
    std::string toGenericString() const;
    
    /**
     * @brief Convert to C-style string
     * 
     * @return Path as C string
     */
    const char* c_str() const;
    
    /**
     * @brief Get underlying std::filesystem::path
     * 
     * @return Reference to filesystem path
     */
    const std::filesystem::path& native() const noexcept;
    
    /**
     * @brief Equality comparison
     */
    bool operator==(const Path& other) const;
    
    /**
     * @brief Inequality comparison
     */
    bool operator!=(const Path& other) const;
    
    /**
     * @brief Less than comparison (for containers)
     */
    bool operator<(const Path& other) const;
    
    /**
     * @brief String conversion operator
     */
    operator std::string() const;
    
    //==========================================================================
    // Utility Functions
    //==========================================================================
    
    /**
     * @brief Set default security level for all Path operations
     * 
     * @param level Default security level
     */
    static void setDefaultSecurityLevel(SecurityLevel level);
    
    /**
     * @brief Get default security level
     * 
     * @return Current default security level
     */
    static SecurityLevel getDefaultSecurityLevel();
    
    /**
     * @brief Register custom path validator
     * 
     * @param name Validator name
     * @param validator Validation function
     */
    static void registerValidator(const std::string& name, 
                                 std::function<bool(const Path&)> validator);
    
    /**
     * @brief Validate path using custom validator
     * 
     * @param validator_name Name of registered validator
     * @return true if validation passes
     */
    bool validateWith(const std::string& validator_name) const;
    
    /**
     * @brief Get system information about path limits
     * 
     * @return Map of system path limits and capabilities
     */
    static std::unordered_map<std::string, std::string> getSystemInfo();
    
    //==========================================================================
    // Integration with AxonVex Components
    //==========================================================================
    
    /**
     * @brief Create configuration file path
     * 
     * @param config_name Configuration name (without extension)
     * @param subdir Optional subdirectory in config dir
     * @return Path to configuration file
     */
    static Path createConfigPath(const std::string& config_name, 
                                const std::string& subdir = "");
    
    /**
     * @brief Create log file path
     * 
     * @param log_name Log name (without extension)
     * @param subdir Optional subdirectory in log dir
     * @return Path to log file
     */
    static Path createLogPath(const std::string& log_name, 
                             const std::string& subdir = "");
    
    /**
     * @brief Create temporary file path
     * 
     * @param prefix File prefix
     * @param extension File extension (with dot)
     * @return Path to unique temporary file
     */
    static Path createTempPath(const std::string& prefix = "axonvex_", 
                              const std::string& extension = ".tmp");

private:
    // Security validation helpers
    bool hasDirectoryTraversal() const;
    bool isInWhitelist() const;
    bool hasIllegalCharacters() const;
    bool exceedsPathLimits() const;
    
    // Cached string for performance
    mutable std::optional<std::string> cached_string_;
    mutable bool string_cache_valid_ = false;
    
    // Thread-safe static data
    static std::mutex static_mutex_;
    static std::unordered_map<std::string, std::function<bool(const Path&)>> custom_validators_;
};

//==============================================================================
// Global Operators and Functions
//==============================================================================

/**
 * @brief Stream output operator
 */
std::ostream& operator<<(std::ostream& os, const Path& path);

/**
 * @brief Join string with path
 */
Path operator/(const std::string& lhs, const Path& rhs);

/**
 * @brief Join C-string with path
 */
Path operator/(const char* lhs, const Path& rhs);

/**
 * @brief Hash specialization for Path (for use in containers)
 */
struct PathHash {
    std::size_t operator()(const Path& path) const {
        return std::hash<std::string>{}(path.toString());
    }
};

} // namespace axonvex::core

// Hash specialization in std namespace
namespace std {
    template<>
    struct hash<axonvex::core::Path> {
        std::size_t operator()(const axonvex::core::Path& path) const {
            return axonvex::core::PathHash{}(path);
        }
    };
}

// Convenient type alias in main namespace
namespace axonvex {
    using Path = core::Path;
} 