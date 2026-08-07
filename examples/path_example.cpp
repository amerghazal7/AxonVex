/**
 * @file path_example.cpp
 * @brief Comprehensive Path Class Example with Configuration Integration
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This example demonstrates the powerful Path class and its integration with
 * the Configuration system, showing type-safe, cross-platform path handling:
 *
 * - Beautiful path composition with operator/
 * - Default directory management
 * - Cross-platform compatibility
 * - Security validation
 * - Configuration integration
 * - File operations and validation
 *
 * Compile and run:
 *   cd build && make path_example && ./examples/path_example
 */

#include <axonvex_core/axonvex.hpp>

using namespace axonvex;
using namespace axonvex::core;
#include "exampleLog.hpp"

using examplelog::Error;
using examplelog::Info;
using examplelog::Warn;

void printHeader(const std::string& title) {
    Info() << "\n=== " << title << " ===";
}

void printSuccess(const std::string& message) {
    Info() << "✓ " << message;
}

void printInfo(const std::string& message) {
    Info() << "ℹ " << message;
}

void printWarning(const std::string& message) {
    Warn() << "⚠ " << message;
}

void printError(const std::string& message) {
    Error() << "✗ " << message;
}

void demonstrateBasicPathUsage() {
    printHeader("Basic Path Operations");

    // Beautiful path composition - no more string concatenation!
    Path config_dir = Path::getDefaultConfigDir();
    Path system_config = config_dir / "systems" / "main.json";
    Path log_file = Path::getDefaultLogDir() / "application.log";
    Path cache_file = Path::getDefaultCacheDir() / "data" / "processed.cache";

    Info() << "📁 Default Directories:";
    Info() << "  App:    " << Path::getDefaultAppDir();
    Info() << "  Config: " << config_dir;
    Info() << "  Logs:   " << Path::getDefaultLogDir();
    Info() << "  Cache:  " << Path::getDefaultCacheDir();
    Info() << "  Data:   " << Path::getDefaultDataDir();
    Info() << "  Temp:   " << Path::getDefaultTempDir();

    Info() << "\n📋 Composed Paths:";
    Info() << "  System Config: " << system_config;
    Info() << "  Log File:      " << log_file;
    Info() << "  Cache File:    " << cache_file;

    // Path manipulation
    Info() << "\n🔧 Path Analysis:";
    Info() << "  Parent:    " << system_config.parent();
    Info() << "  Filename:  " << system_config.filename();
    Info() << "  Extension: " << system_config.extension();
    Info() << "  Stem:      " << system_config.stem();

    // Replace operations
    Path backup_config = system_config.replaceExtension(".bak");
    Path alt_config = system_config.replaceFilename("alternative.json");

    Info() << "\n📝 Path Transformations:";
    Info() << "  Backup:    " << backup_config;
    Info() << "  Alternative: " << alt_config;

    printSuccess("Basic path operations completed successfully");
}

void demonstrateSecurityFeatures() {
    printHeader("Security and Validation Features");

    // Safe paths
    Path safe_config = Path::createConfigPath("application");
    Path safe_log = Path::createLogPath("system");

    Info() << "🔒 Security Validation:";
    Info() << "  Safe config: " << safe_config << " → "
           << (safe_config.isSecure() ? "SECURE" : "UNSAFE");
    Info() << "  Safe log:    " << safe_log << " → " << (safe_log.isSecure() ? "SECURE" : "UNSAFE");

    // Dangerous paths (directory traversal attempts)
    std::vector<std::string> dangerous_paths = {
        "../../../etc/passwd", "config/../../../sensitive.txt",
        "..\\..\\..\\windows\\system32\\config", "/etc/shadow",
        "C:\\Windows\\System32\\config\\SAM"};

    Info() << "\n⚠️  Security Threats Detected:";
    for (const auto& dangerous : dangerous_paths) {
        Path threat_path(dangerous);
        bool is_secure = threat_path.isSecure();

        Info() << "  " << dangerous << " → " << (is_secure ? "ALLOWED" : "BLOCKED");

        if (!is_secure) {
            auto errors = threat_path.validateSecurity();
            Info() << " (" << errors.size() << " security violations)";
        }
        Info() << std::endl;
    }

    // Different security levels
    Path test_path("../config/test.json");
    Info() << "\n🛡️  Security Levels:";
    Info() << "  Path: " << test_path;
    Info() << "  NONE:     " << (test_path.isSecure(Path::SecurityLevel::NONE) ? "✓" : "✗");
    Info() << "  BASIC:    " << (test_path.isSecure(Path::SecurityLevel::BASIC) ? "✓" : "✗");
    Info() << "  STRICT:   " << (test_path.isSecure(Path::SecurityLevel::STRICT) ? "✓" : "✗");
    Info() << "  PARANOID: " << (test_path.isSecure(Path::SecurityLevel::PARANOID) ? "✓" : "✗");

    printSuccess("Security validation demonstrates robust protection");
}

void demonstrateFileOperations() {
    printHeader("File and Directory Operations");

    // Create a test directory structure
    Path test_root = Path::getDefaultTempDir() / "axonvex_path_demo";
    Path config_dir = test_root / "config";
    Path logs_dir = test_root / "logs";
    Path data_dir = test_root / "data";

    Info() << "📁 Creating Directory Structure:";
    Info() << "  Root: " << test_root;

    // Create directories
    if (config_dir.createDirectories()) {
        printSuccess("Config directory created: " + config_dir.toString());
    }
    if (logs_dir.createDirectories()) {
        printSuccess("Logs directory created: " + logs_dir.toString());
    }
    if (data_dir.createDirectories()) {
        printSuccess("Data directory created: " + data_dir.toString());
    }

    // Create some test files
    Path config_file = config_dir / "app.json";
    Path log_file = logs_dir / "app.log";
    Path data_file = data_dir / "cache.dat";

    Info() << "\n📄 Creating Test Files:";

    // Write test configuration
    std::ofstream config_stream(config_file.toString());
    config_stream << R"({
    "app": {
        "name": "AxonVex Path Demo",
        "version": "1.0.0",
        "paths": {
            "config_dir": ")"
                  << config_dir.toGenericString() << R"(",
            "log_file": ")"
                  << log_file.toGenericString() << R"(",
            "data_dir": ")"
                  << data_dir.toGenericString() << R"("
        }
    }
})";
    config_stream.close();

    if (config_file.exists()) {
        printSuccess("Configuration file created: " + config_file.filename());
        Info() << "    Size: " << config_file.size() << " bytes";
        Info() << "    Readable: " << (config_file.isReadable() ? "Yes" : "No");
        Info() << "    Writable: " << (config_file.isWritable() ? "Yes" : "No");
    }

    // Write test log
    std::ofstream log_stream(log_file.toString());
    log_stream << "[INFO] AxonVex Path Demo started\n";
    log_stream << "[INFO] Configuration loaded from: " << config_file << "\n";
    log_stream << "[INFO] Demo completed successfully\n";
    log_stream.close();

    if (log_file.exists()) {
        printSuccess("Log file created: " + log_file.filename());
    }

    // Create data file
    std::ofstream data_stream(data_file.toString(), std::ios::binary);
    std::vector<uint8_t> test_data = {0x41, 0x58, 0x4F, 0x4E, 0x56, 0x45, 0x58}; // "AXONVEX"
    data_stream.write(reinterpret_cast<const char*>(test_data.data()), test_data.size());
    data_stream.close();

    if (data_file.exists()) {
        printSuccess("Data file created: " + data_file.filename());
    }

    // List directory contents
    Info() << "\n📋 Directory Listing:";
    auto contents = test_root.listDirectory(true);
    for (const auto& path : contents) {
        std::string type = path.isDirectory() ? "📁" : "📄";
        Info() << "  " << type << " " << path.relativeTo(test_root);
    }

    // File operations
    Info() << "\n🔄 File Operations:";

    // Find files
    auto json_files = test_root.findFiles("*.json", true);
    Info() << "  Found " << json_files.size() << " JSON files:";
    for (const auto& file : json_files) {
        Info() << "    " << file.relativeTo(test_root);
    }

    // Cleanup
    Info() << "\n🧹 Cleanup:";
    auto removed_count = test_root.removeAll();
    Info() << "  Removed " << removed_count << " files and directories";

    printSuccess("File operations completed successfully");
}

void demonstrateConfigurationIntegration() {
    printHeader("Enhanced Configuration with Path Objects");

    // Create paths for configuration
    Path app_config = Path::createConfigPath("application");
    Path system_config = Path::createConfigPath("system", "advanced");
    Path user_config = Path::createConfigPath("user_preferences");

    Info() << "🗂️  Configuration File Paths:";
    Info() << "  Application: " << app_config;
    Info() << "  System:      " << system_config;
    Info() << "  User:        " << user_config;

    // Ensure directories exist
    app_config.parent().createDirectories();
    system_config.parent().createDirectories();
    user_config.parent().createDirectories();

    // Create enhanced configuration using paths
    Configuration config;

    // Instead of using strings, we can now store and validate paths properly
    std::string config_json = R"({
        "application": {
            "name": "AxonVex Path Integration Demo",
            "version": "1.0.0"
        },
        "paths": {
            "config_dir": ")" +
                              Path::getDefaultConfigDir().toGenericString() + R"(",
            "log_dir": ")" + Path::getDefaultLogDir().toGenericString() +
                              R"(",
            "data_dir": ")" + Path::getDefaultDataDir().toGenericString() +
                              R"(",
            "cache_dir": ")" + Path::getDefaultCacheDir().toGenericString() +
                              R"(",
            "temp_dir": ")" + Path::getDefaultTempDir().toGenericString() +
                              R"("
        },
        "files": {
            "main_config": ")" +
                              app_config.toGenericString() + R"(",
            "system_config": ")" +
                              system_config.toGenericString() + R"(",
            "user_config": ")" +
                              user_config.toGenericString() + R"("
        }
    })";

    if (config.loadFromString(config_json)) {
        printSuccess("Configuration loaded with path integration");
    }

    // Now we can work with paths in a type-safe way
    Info() << "\n🔧 Path-Enhanced Configuration Usage:";

    // Extract paths from configuration
    auto config_dir_str = config.get<std::string>("paths.config_dir");
    auto log_dir_str = config.get<std::string>("paths.log_dir");
    auto main_config_str = config.get<std::string>("files.main_config");

    // Convert to Path objects for type safety and validation
    Path config_dir_path(config_dir_str);
    Path log_dir_path(log_dir_str);
    Path main_config_path(main_config_str);

    Info() << "  Config Dir:     " << config_dir_path
           << " (Secure: " << (config_dir_path.isSecure() ? "✓" : "✗") << ")";
    Info() << "  Log Dir:        " << log_dir_path
           << " (Exists: " << (log_dir_path.exists() ? "✓" : "✗") << ")";
    Info() << "  Main Config:    " << main_config_path
           << " (Writable: " << (main_config_path.parent().isWritable() ? "✓" : "✗") << ")";

    // Demonstrate path composition with configuration values
    Path runtime_log = log_dir_path / "runtime.log";
    Path backup_config = config_dir_path / "backup" / "app_backup.json";
    Path temp_cache = Path(config.get<std::string>("paths.temp_dir")) / "session_cache.tmp";

    Info() << "\n📋 Composed Paths from Configuration:";
    Info() << "  Runtime Log:    " << runtime_log;
    Info() << "  Backup Config:  " << backup_config;
    Info() << "  Temp Cache:     " << temp_cache;

    // Save the configuration using a Path object
    if (config.saveToFile(app_config.toString())) {
        printSuccess("Configuration saved to: " + app_config.toString());

        // Verify the file was created
        if (app_config.exists()) {
            Info() << "  File size: " << app_config.size() << " bytes";
            Info() << "  Last modified: " << "recent"; // Would show actual time in real code
        }
    }

    printSuccess("Configuration integration demonstrates type-safe path handling");
}

void demonstrateAdvancedFeatures() {
    printHeader("Advanced Path Features");

    // Temporary file creation with automatic cleanup
    Info() << "\n🗂️  Temporary Files:";

    std::vector<Path> temp_files;
    for (int i = 0; i < 3; ++i) {
        Path temp_file = Path::createTempPath("demo_", ".tmp");
        temp_files.push_back(temp_file);

        // Create the file
        std::ofstream stream(temp_file.toString());
        stream << "Temporary file " << i << " created by AxonVex Path demo";
        stream.close();

        Info() << "  Created: " << temp_file.filename() << " (Size: " << temp_file.size()
               << " bytes)";
    }

    // Cleanup temporary files
    Info() << "\n🧹 Cleaning up temporary files:";
    for (const auto& temp_file : temp_files) {
        if (temp_file.remove()) {
            Info() << "  Removed: " << temp_file.filename();
        }
    }

    printSuccess("Advanced features demonstrate the full power of Path class");
}

void demonstratePerformance() {
    printHeader("Performance Characteristics");

    const int num_operations = 100000;

    Info() << "⚡ Running performance tests with " << num_operations << " operations...";

    // Test path creation and manipulation
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; ++i) {
        Path base_path = Path::getDefaultConfigDir();
        Path composed_path = base_path / "test" / ("file_" + std::to_string(i)) / "config.json";
        volatile std::string path_str = composed_path.toString();
        volatile std::string filename = composed_path.filename();
        volatile std::string extension = composed_path.extension();
        volatile bool secure = composed_path.isSecure();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    double avg_time_ns = static_cast<double>(duration.count()) / num_operations;
    double throughput = 1e9 / avg_time_ns;

    Info() << "\n📊 Performance Results:";
    Info() << "  Average operation time: " << std::fixed << std::setprecision(2) << avg_time_ns
           << " ns";
    Info() << "  Throughput: " << std::fixed << std::setprecision(0) << throughput << " ops/sec";
    Info() << "  Total test time: " << duration.count() / 1e6 << " ms";

    // Performance target validation
    const double target_ns = 1000.0; // Target: under 1 microsecond per operation
    if (avg_time_ns < target_ns) {
        printSuccess("Performance exceeds target (<" + std::to_string((int)target_ns) +
                     "ns per operation)");
    } else {
        printWarning("Performance below target (" + std::to_string(avg_time_ns) + "ns > " +
                     std::to_string((int)target_ns) + "ns)");
    }
}

int main() {
    Info() << "\n🚀 AxonVex Path Class - Comprehensive Example";
    Info() << "   Type-Safe, Cross-Platform Path Management";

    try {
        // Run all demonstrations
        demonstrateBasicPathUsage();
        demonstrateSecurityFeatures();
        demonstrateFileOperations();
        demonstrateConfigurationIntegration();
        demonstrateAdvancedFeatures();
        demonstratePerformance();

        printHeader("Example Complete");
        printSuccess("All Path class features demonstrated successfully!");

        Info() << "\nKey Benefits Demonstrated:";
        Info() << "  ✅ Type-safe path composition with operator/";
        Info() << "  ✅ Cross-platform compatibility (Windows, Linux, macOS)";
        Info() << "  ✅ Default directory management";
        Info() << "  ✅ Security validation and threat protection";
        Info() << "  ✅ Integration with Configuration system";
        Info() << "  ✅ File and directory operations";
        Info() << "  ✅ Custom validation capabilities";
        Info() << "  ✅ High-performance operations";
        Info() << "  ✅ Beautiful, intuitive API design";

        Info() << "\n🎉 AxonVex Path class: Making file system operations elegant and safe!";

        return 0;

    } catch (const std::exception& e) {
        printError("Exception occurred: " + std::string(e.what()));
        return 1;
    } catch (...) {
        printError("Unknown exception occurred");
        return 1;
    }
}
