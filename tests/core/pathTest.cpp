/**
 * @file pathTest.cpp
 * @brief Comprehensive test suite for Path class
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This file contains comprehensive tests for the Path class including:
 * - Cross-platform path handling
 * - Default directory management
 * - Path composition and manipulation
 * - Security validation
 * - File and directory operations
 * - Integration with AxonVex components
 */

#include <axonvex_core/path.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace axonvex::core;

namespace {
long testProcessId() {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}
} // namespace

class PathTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create test directory structure
        test_root = Path::getDefaultTempDir() / ("axonvex_path_test_" + std::to_string(testProcessId()));
        test_root.createDirectories();

        test_file = test_root / "test_file.txt";
        test_dir = test_root / "test_directory";
        test_subdir = test_dir / "subdirectory";

        // Create test file
        std::ofstream file(test_file.toString());
        file << "Test content";
        file.close();

        // Create test directories
        test_dir.createDirectories();
        test_subdir.createDirectories();
    }

    void TearDown() override {
        // Clean up test files and directories
        if (test_root.exists()) {
            test_root.removeAll();
        }
    }

    Path test_root;
    Path test_file;
    Path test_dir;
    Path test_subdir;
};

//==============================================================================
// Construction and Basic Operations Tests
//==============================================================================

TEST_F(PathTest, ConstructionTest) {
    // Default construction
    Path empty_path;
    EXPECT_TRUE(empty_path.empty());

    // String construction
    Path string_path("test/path");
    EXPECT_FALSE(string_path.empty());
    EXPECT_EQ(string_path.toString(), "test/path");

    // C-string construction
    Path cstring_path("/absolute/path");
    EXPECT_TRUE(cstring_path.isAbsolute());

    // Copy construction
    Path copy_path(string_path);
    EXPECT_EQ(copy_path.toString(), string_path.toString());

    // Move construction
    Path original("move/test");
    Path moved_path(std::move(original));
    EXPECT_EQ(moved_path.toString(), "move/test");
}

TEST_F(PathTest, PathCompositionTest) {
    Path base("base");
    Path component("component");

    // Operator/ with Path
    Path composed = base / component;
    EXPECT_NE(composed.toString().find("base"), std::string::npos);
    EXPECT_NE(composed.toString().find("component"), std::string::npos);

    // Operator/ with string
    Path string_composed = base / "string_component";
    EXPECT_NE(string_composed.toString().find("string_component"), std::string::npos);

    // Operator/ with C-string
    Path cstring_composed = base / "cstring_component";
    EXPECT_NE(cstring_composed.toString().find("cstring_component"), std::string::npos);

    // In-place composition
    Path in_place("start");
    in_place /= "middle";
    in_place /= "end";
    EXPECT_NE(in_place.toString().find("start"), std::string::npos);
    EXPECT_NE(in_place.toString().find("middle"), std::string::npos);
    EXPECT_NE(in_place.toString().find("end"), std::string::npos);
}

//==============================================================================
// Default Directory Tests
//==============================================================================

TEST_F(PathTest, DefaultDirectoriesTest) {
    // Test all default directory types
    auto app_dir = Path::getDefaultAppDir();
    auto config_dir = Path::getDefaultConfigDir();
    auto log_dir = Path::getDefaultLogDir();
    auto cache_dir = Path::getDefaultCacheDir();
    auto data_dir = Path::getDefaultDataDir();
    auto temp_dir = Path::getDefaultTempDir();

    EXPECT_FALSE(app_dir.empty());
    EXPECT_FALSE(config_dir.empty());
    EXPECT_FALSE(log_dir.empty());
    EXPECT_FALSE(cache_dir.empty());
    EXPECT_FALSE(data_dir.empty());
    EXPECT_FALSE(temp_dir.empty());

    // Test generic getDefaultDir
    auto generic_app = Path::getDefaultDir(Path::DefaultDir::APP);
    EXPECT_EQ(app_dir.toString(), generic_app.toString());

    // Test custom directory setting
    Path custom_path("/custom/test/path");
    Path::setDefaultDir(Path::DefaultDir::CACHE, custom_path);
    auto new_cache_dir = Path::getDefaultCacheDir();
    EXPECT_EQ(new_cache_dir.toString(), custom_path.toString());

    // Reset to defaults
    Path::resetDefaultDirs();
    auto reset_cache_dir = Path::getDefaultCacheDir();
    EXPECT_NE(reset_cache_dir.toString(), custom_path.toString());
}

//==============================================================================
// Path Manipulation Tests
//==============================================================================

TEST_F(PathTest, PathManipulationTest) {
    Path test_path = test_file;

    // Parent directory
    Path parent = test_path.parent();
    EXPECT_EQ(parent.toString(), test_root.toString());

    // Filename
    std::string filename = test_path.filename();
    EXPECT_EQ(filename, "test_file.txt");

    // Extension
    std::string extension = test_path.extension();
    EXPECT_EQ(extension, ".txt");

    // Stem (filename without extension)
    std::string stem = test_path.stem();
    EXPECT_EQ(stem, "test_file");

    // Replace extension
    Path new_ext = test_path.replaceExtension(".json");
    EXPECT_EQ(new_ext.extension(), ".json");
    EXPECT_EQ(new_ext.stem(), "test_file");

    // Replace filename
    Path new_name = test_path.replaceFilename("new_name.xml");
    EXPECT_EQ(new_name.filename(), "new_name.xml");
    EXPECT_EQ(new_name.parent().toString(), test_root.toString());

    // Absolute path
    Path abs_path = test_path.absolute();
    EXPECT_TRUE(abs_path.isAbsolute());

    // Normalize path
    Path with_dots = test_root / ".." / test_root.filename() / "." / "test_file.txt";
    Path normalized = with_dots.normalize();
    // Should resolve to something equivalent to test_file
}

//==============================================================================
// Path Properties Tests
//==============================================================================

TEST_F(PathTest, PathPropertiesTest) {
    // Exists test
    EXPECT_TRUE(test_file.exists());
    EXPECT_TRUE(test_dir.exists());
    EXPECT_FALSE((test_root / "nonexistent.file").exists());

    // File vs directory
    EXPECT_TRUE(test_file.isFile());
    EXPECT_FALSE(test_file.isDirectory());
    EXPECT_FALSE(test_dir.isFile());
    EXPECT_TRUE(test_dir.isDirectory());

    // Empty test
    Path empty_path;
    EXPECT_TRUE(empty_path.empty());
    EXPECT_FALSE(test_file.empty());

    // Absolute vs relative
    Path abs_path("/absolute/path");
    Path rel_path("relative/path");

#ifdef _WIN32
    // On Windows, check for drive letter
    if (abs_path.toString().find(":") != std::string::npos) {
        EXPECT_TRUE(abs_path.isAbsolute());
    }
#else
    EXPECT_TRUE(abs_path.isAbsolute());
#endif
    EXPECT_TRUE(rel_path.isRelative());

    // Size test
    auto file_size = test_file.size();
    EXPECT_GT(file_size, 0); // Should have some content

    // Readable/writable tests
    EXPECT_TRUE(test_file.isReadable());
    EXPECT_TRUE(test_file.isWritable());
    EXPECT_TRUE(test_dir.isReadable());
    EXPECT_TRUE(test_dir.isWritable());
}

//==============================================================================
// Security Validation Tests
//==============================================================================

TEST_F(PathTest, SecurityValidationTest) {
    // Safe paths
    Path safe_path = test_root / "safe_file.txt";
    EXPECT_TRUE(safe_path.isSecure());
    EXPECT_TRUE(safe_path.isSecure(Path::SecurityLevel::BASIC));

    // Directory traversal attempts
    Path traversal_path("../../../etc/passwd");
    EXPECT_FALSE(traversal_path.isSecure());

    Path traversal_path2("safe/../../../dangerous");
    EXPECT_FALSE(traversal_path2.isSecure());

    // Test validation levels
    EXPECT_TRUE(safe_path.isSecure(Path::SecurityLevel::NONE));
    EXPECT_TRUE(safe_path.isSecure(Path::SecurityLevel::BASIC));

    // Get validation errors
    auto errors = traversal_path.validateSecurity();
    EXPECT_GT(errors.size(), 0);

    auto safe_errors = safe_path.validateSecurity();
    EXPECT_EQ(safe_errors.size(), 0);

    // Test default security level
    Path::setDefaultSecurityLevel(Path::SecurityLevel::STRICT);
    EXPECT_EQ(Path::getDefaultSecurityLevel(), Path::SecurityLevel::STRICT);

    Path::setDefaultSecurityLevel(Path::SecurityLevel::BASIC);
}

//==============================================================================
// Directory Operations Tests
//==============================================================================

TEST_F(PathTest, DirectoryOperationsTest) {
    Path new_dir = test_root / "new_directory";

    // Create directory
    EXPECT_FALSE(new_dir.exists());
    EXPECT_TRUE(new_dir.createDirectory());
    EXPECT_TRUE(new_dir.exists());
    EXPECT_TRUE(new_dir.isDirectory());

    // Create nested directories
    Path nested_dir = test_root / "level1" / "level2" / "level3";
    EXPECT_TRUE(nested_dir.createDirectories());
    EXPECT_TRUE(nested_dir.exists());

    // List directory contents
    auto contents = test_root.listDirectory();
    EXPECT_GT(contents.size(), 0);

    bool found_test_file = false;
    bool found_test_dir = false;
    for (const auto& path : contents) {
        if (path.filename() == "test_file.txt") {
            found_test_file = true;
        }
        if (path.filename() == "test_directory") {
            found_test_dir = true;
        }
    }
    EXPECT_TRUE(found_test_file);
    EXPECT_TRUE(found_test_dir);

    // Recursive listing
    auto recursive_contents = test_root.listDirectory(true);
    EXPECT_GE(recursive_contents.size(), contents.size());

    // Find files with pattern
    auto txt_files = test_root.findFiles("*.txt");
    EXPECT_GE(txt_files.size(), 1);

    // Remove directory
    EXPECT_TRUE(new_dir.remove());
    EXPECT_FALSE(new_dir.exists());
}

//==============================================================================
// File Operations Tests
//==============================================================================

TEST_F(PathTest, FileOperationsTest) {
    Path source_file = test_file;
    Path dest_file = test_root / "copied_file.txt";

    // Copy file
    EXPECT_TRUE(source_file.copyTo(dest_file));
    EXPECT_TRUE(dest_file.exists());
    EXPECT_TRUE(dest_file.isFile());

    // Check copied content
    std::ifstream copied_file(dest_file.toString());
    std::string content;
    std::getline(copied_file, content);
    EXPECT_EQ(content, "Test content");

    // Copy with overwrite test
    Path existing_dest = dest_file;
    EXPECT_FALSE(source_file.copyTo(existing_dest, false)); // Should fail without overwrite
    EXPECT_TRUE(source_file.copyTo(existing_dest, true));   // Should succeed with overwrite

    // Move file
    Path moved_file = test_root / "moved_file.txt";
    EXPECT_TRUE(dest_file.moveTo(moved_file));
    EXPECT_FALSE(dest_file.exists());
    EXPECT_TRUE(moved_file.exists());

    // Create backup
    Path backup_file = source_file.createBackup();
    EXPECT_TRUE(backup_file.exists());
    EXPECT_NE(backup_file.toString().find(".bak"), std::string::npos);

    // Get unique filename
    Path unique_file = source_file.getUniqueFilename();
    if (source_file.exists()) {
        EXPECT_NE(unique_file.toString(), source_file.toString());
    }
}

//==============================================================================
// String Conversion and Comparison Tests
//==============================================================================

TEST_F(PathTest, StringConversionTest) {
    Path test_path("test/path/file.txt");

    // String conversion
    std::string path_str = test_path.toString();
    EXPECT_EQ(path_str, "test/path/file.txt");

    // Generic string (forward slashes)
    std::string generic_str = test_path.toGenericString();
    EXPECT_NE(generic_str.find('/'), std::string::npos);

    // C-string conversion
    const char* c_str = test_path.c_str();
    EXPECT_STREQ(c_str, path_str.c_str());

    // String conversion operator
    std::string implicit_str = test_path;
    EXPECT_EQ(implicit_str, path_str);

    // Comparison operators
    Path path1("same/path");
    Path path2("same/path");
    Path path3("different/path");

    EXPECT_TRUE(path1 == path2);
    EXPECT_FALSE(path1 == path3);
    EXPECT_FALSE(path1 != path2);
    EXPECT_TRUE(path1 != path3);

    // Less than comparison (for containers)
    EXPECT_TRUE(path1 < path3 || path3 < path1); // One should be less than the other
}

//==============================================================================
// Integration with AxonVex Components Tests
//==============================================================================

TEST_F(PathTest, AxonVexIntegrationTest) {
    // Create configuration path
    Path config_path = Path::createConfigPath("system");
    EXPECT_EQ(config_path.extension(), ".json");
    EXPECT_NE(config_path.toString().find("config"), std::string::npos);
    EXPECT_EQ(config_path.filename(), "system.json");

    // Create config path with subdirectory
    Path subdir_config = Path::createConfigPath("advanced", "subsystem");
    EXPECT_NE(subdir_config.toString().find("subsystem"), std::string::npos);
    EXPECT_EQ(subdir_config.filename(), "advanced.json");

    // Create log path
    Path log_path = Path::createLogPath("application");
    EXPECT_EQ(log_path.extension(), ".log");
    EXPECT_NE(log_path.toString().find("log"), std::string::npos);
    EXPECT_EQ(log_path.filename(), "application.log");

    // Create temporary path
    Path temp_path1 = Path::createTempPath();
    Path temp_path2 = Path::createTempPath();

    EXPECT_NE(temp_path1.toString(), temp_path2.toString()); // Should be unique
    EXPECT_EQ(temp_path1.extension(), ".tmp");
    EXPECT_NE(temp_path1.toString().find("axonvex_"), std::string::npos);

    // Custom temporary path
    Path custom_temp = Path::createTempPath("custom_prefix_", ".data");
    EXPECT_EQ(custom_temp.extension(), ".data");
    EXPECT_NE(custom_temp.toString().find("custom_prefix_"), std::string::npos);
}

//==============================================================================
// Custom Validator Tests
//==============================================================================

TEST_F(PathTest, CustomValidatorTest) {
    // Register custom validator
    Path::registerValidator("test_validator", [](const Path& path) {
        return path.toString().find("allowed") != std::string::npos;
    });

    Path allowed_path("allowed/path");
    Path disallowed_path("forbidden/path");

    EXPECT_TRUE(allowed_path.validateWith("test_validator"));
    EXPECT_FALSE(disallowed_path.validateWith("test_validator"));

    // Non-existent validator
    EXPECT_FALSE(allowed_path.validateWith("nonexistent_validator"));
}

//==============================================================================
// System Information Tests
//==============================================================================

TEST_F(PathTest, SystemInfoTest) {
    auto system_info = Path::getSystemInfo();

    EXPECT_GT(system_info.size(), 0);
    EXPECT_NE(system_info.find("current_path"), system_info.end());
    EXPECT_NE(system_info.find("temp_directory"), system_info.end());
    EXPECT_NE(system_info.find("max_path_length"), system_info.end());
    EXPECT_NE(system_info.find("separator"), system_info.end());
    EXPECT_NE(system_info.find("case_sensitive"), system_info.end());
}

//==============================================================================
// Performance Tests
//==============================================================================

TEST_F(PathTest, PerformanceTest) {
    const int num_operations = 10000;

    // Test path creation performance
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; ++i) {
        Path test_path("performance/test/path" + std::to_string(i));
        volatile std::string str = test_path.toString();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    double avg_time_ns = static_cast<double>(duration.count()) / num_operations;

    // Should be very fast - under 1000ns per operation
    EXPECT_LT(avg_time_ns, 1000.0) << "Average path creation time: " << avg_time_ns << " ns";

    std::cout << "\nPath Performance Results:" << std::endl;
    std::cout << "  Average path creation time: " << avg_time_ns << " ns" << std::endl;
    std::cout << "  Throughput: " << (1e9 / avg_time_ns) << " ops/sec" << std::endl;
}

//==============================================================================
// Thread Safety Tests
//==============================================================================

TEST_F(PathTest, ThreadSafetyTest) {
    const int num_threads = 8;
    const int operations_per_thread = 1000;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    // Test concurrent operations
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                try {
                    // Mix of operations
                    if (i % 3 == 0) {
                        Path path = Path::getDefaultTempDir() / ("thread_" + std::to_string(t)) /
                                    ("file_" + std::to_string(i));
                        volatile bool exists = path.exists();
                    } else if (i % 3 == 1) {
                        Path config_path = Path::createConfigPath(
                            "thread_test_" + std::to_string(t) + "_" + std::to_string(i));
                        volatile std::string str = config_path.toString();
                    } else {
                        Path temp_path = Path::createTempPath("thread_" + std::to_string(t) + "_");
                        volatile bool secure = temp_path.isSecure();
                    }
                } catch (const std::exception&) { errors.fetch_add(1); }
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Should have no errors
    EXPECT_EQ(errors.load(), 0) << "Thread safety test failed with " << errors.load() << " errors";
}

//==============================================================================
// Global Operators Tests
//==============================================================================

TEST_F(PathTest, GlobalOperatorsTest) {
    // Stream output operator
    Path test_path("stream/test/path");
    std::stringstream ss;
    ss << test_path;
    EXPECT_EQ(ss.str(), test_path.toString());

    // Global path joining operators
    Path path1 = "string_base" / Path("path_component");
    EXPECT_NE(path1.toString().find("string_base"), std::string::npos);
    EXPECT_NE(path1.toString().find("path_component"), std::string::npos);

    Path path2 = "cstring_base" / Path("path_component");
    EXPECT_NE(path2.toString().find("cstring_base"), std::string::npos);

    // Hash functionality (for containers)
    std::unordered_set<Path> path_set;
    path_set.insert(Path("path1"));
    path_set.insert(Path("path2"));
    path_set.insert(Path("path1")); // Duplicate

    EXPECT_EQ(path_set.size(), 2); // Should only have 2 unique paths
}
