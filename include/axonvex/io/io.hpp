/**
 * @file io.hpp
 * @brief AxonVex IO Module - Input/Output operations and data streaming
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

// File operations
#include <axonvex/io/files/fileManager.hpp>

// Data streaming
#include <axonvex/io/streams/dataStream.hpp>

// Network operations (placeholder)
// #include <axonvex/io/network/networkManager.hpp>

// Device I/O (placeholder)
// #include <axonvex/io/devices/deviceManager.hpp>

// Protocol handling (placeholder)
// #include <axonvex/io/protocols/protocolHandler.hpp>

namespace axonvex::io {
    
    // Re-export commonly used types for convenience
    using FileManager = files::FileManager;
    using FileHandle = files::FileHandle;
    
    template<typename T>
    using DataStream = streams::DataStream<T>;
    
    template<typename TInput, typename TOutput>
    using StreamProcessor = streams::StreamProcessor<TInput, TOutput>;
    
    // Module version information
    constexpr int IO_MODULE_VERSION_MAJOR = 1;
    constexpr int IO_MODULE_VERSION_MINOR = 0;
    constexpr int IO_MODULE_VERSION_PATCH = 0;
    
    // Module information
    inline const char* getIOModuleVersion() {
        return "1.0.0";
    }
    
    inline const char* getIOModuleDescription() {
        return "AxonVex IO Module - Input/Output operations, file handling, and data streaming";
    }
    
    /**
     * @brief Initialize the IO module
     */
    inline void initialize() {
        // Initialize any global IO components if needed
    }

    /**
     * @brief Cleanup the IO module
     */
    inline void cleanup() {
        // Cleanup any global IO components if needed
    }

} // namespace axonvex::io