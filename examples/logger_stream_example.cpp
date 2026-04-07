/**
 * @file logger_stream_example.cpp
 * @brief Example demonstrating both traditional and stream-based logging
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <array>
#include <axonvex_core/axonvex.hpp>
#include <chrono>
#include <thread>
#include <vector>

using namespace axonvex;
using namespace axonvex::Log;

int main() {
    axonvex::printWelcome();

    std::cout << "\n=== AxonVex Logger Stream Interface Demo ===" << std::endl;

    // Configure global logger
    Log::setLevel(LogLevel::Debug);
    Log::addFileOutput("stream_example.log");

    std::cout << "\n1. Basic Stream-Based Logging:" << std::endl;

    // Basic stream-based logging (the new user-friendly interface)
    Log::Info() << "System initialized successfully!";
    Log::Warn() << "Temperature sensor reading: " << 45.7 << "°C";
    Log::Error() << "Network connection failed with code: " << 404;
    Log::Critical() << "Critical system failure detected!";
    Log::Debug() << "Debug message with multiple values: " << 123 << " " << 456;

    std::cout << "\n2. Container Logging:" << std::endl;

    // Container logging (vectors, arrays)
    std::vector<int> data = {1, 2, 3, 4, 5};
    std::array<double, 3> position = {1.5, 2.3, 4.7};

    Log::Info() << "Data vector: " << data;
    Log::Info() << "Position array: " << position;

    std::cout << "\n3. Performance Comparison:" << std::endl;

    // Performance comparison
    const int num_messages = 1000;

    // Traditional function-based logging
    auto start_time = std::chrono::high_resolution_clock::now();

    Logger traditional_logger;
    auto console_output = std::make_shared<ConsoleOutput>();
    traditional_logger.addOutput(console_output);
    traditional_logger.start();

    for (int i = 0; i < num_messages; ++i) {
        traditional_logger.info("Perf", "Traditional message " + std::to_string(i));
    }

    auto mid_time = std::chrono::high_resolution_clock::now();

    // Stream-based logging (performance test)
    for (int i = 0; i < num_messages; ++i) {
        Log::Info() << "Stream message " << i;
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    traditional_logger.stop();

    auto traditional_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(mid_time - start_time);
    auto stream_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - mid_time);

    std::cout << "\nPerformance Results:" << std::endl;
    std::cout << "Traditional logging: " << traditional_duration.count() << " μs" << std::endl;
    std::cout << "Stream-based logging: " << stream_duration.count() << " μs" << std::endl;

    std::cout << "\n4. Multi-threaded Stream Logging:" << std::endl;

    // Multi-threaded stream logging
    std::vector<std::thread> threads;
    const int num_threads = 4;
    const int messages_per_thread = 10;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, messages_per_thread]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                Log::Info() << "Thread " << t << " message " << i;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::cout << "\n5. File Logging:" << std::endl;

    // File logging
    Log::File() << "This message goes to file: "
                << std::chrono::system_clock::now().time_since_epoch().count();
    Log::File() << "File logging with data: " << data;

    std::cout << "\n6. Traditional API Still Available:" << std::endl;

    // Traditional API for backward compatibility
    Logger& global_logger = Log::getLogger();
    global_logger.info("Traditional", "This uses the traditional API");
    global_logger.warning("Traditional", "Warning via traditional API");

    // Using convenience macros
    LOG_INFO(global_logger, "Macro", "This uses the convenience macro");
    LOG_ERROR(global_logger, "Macro", "Error via macro");

    std::cout << "\n7. Logger Statistics:" << std::endl;

    // Display statistics
    const auto& stats = global_logger.getStatistics();
    std::cout << "Messages logged: " << stats.getMessagesLogged() << std::endl;
    std::cout << "Average processing time: " << stats.getAverageProcessingTimeNs() << " ns"
              << std::endl;
    std::cout << "Peak queue size: " << stats.getPeakQueueSize() << std::endl;

    std::cout << "\n=== Demo Complete ===" << std::endl;

    // Allow time for async logging to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return 0;
}
