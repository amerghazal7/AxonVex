/**
 * @file dataStream.hpp
 * @brief Data Streaming for AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <axonvex/core/threadSafeQueue.hpp>
#include <axonvex/core/circularBuffer.hpp>
#include <axonvex/core/processingUnit.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <chrono>

namespace axonvex::io::streams {

/**
 * @brief High-performance data streaming with backpressure handling
 */
template<typename T>
class DataStream : public axonvex::core::ProcessingUnit {
public:
    using DataCallback = std::function<void(const T&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    explicit DataStream(size_t bufferSize = 1000);
    ~DataStream();

    // Data operations
    bool write(const T& data);
    bool read(T& data);
    bool hasData() const;
    size_t availableData() const;
    size_t capacity() const;

    // Stream control
    void start();
    void stop();
    void pause();
    void resume();
    bool isRunning() const;
    bool isPaused() const;

    // Callback registration
    void setDataCallback(DataCallback callback);
    void setErrorCallback(ErrorCallback callback);

    // Performance monitoring
    double getThroughput() const;
    double getLatency() const;
    size_t getDroppedData() const;
    void resetStatistics();

    // ProcessingUnit implementation
    void processSync() override;
    void processAsync() override;

    // Flow control
    void setBackpressureThreshold(double threshold);
    bool isBackpressureActive() const;

private:
    std::unique_ptr<axonvex::core::CircularBuffer<T>> buffer_;
    DataCallback dataCallback_;
    ErrorCallback errorCallback_;

    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<size_t> droppedData_;
    double backpressureThreshold_;

    // Performance tracking
    mutable std::chrono::high_resolution_clock::time_point lastWrite_;
    mutable std::chrono::high_resolution_clock::time_point lastRead_;
    mutable std::atomic<size_t> writeCount_;
    mutable std::atomic<size_t> readCount_;
    mutable std::chrono::high_resolution_clock::time_point startTime_;

    void updateStatistics();
    bool checkBackpressure() const;
};

/**
 * @brief Stream processor for pipeline operations
 */
template<typename TInput, typename TOutput>
class StreamProcessor : public axonvex::core::ProcessingUnit {
public:
    using ProcessorFunction = std::function<TOutput(const TInput&)>;

    StreamProcessor(std::shared_ptr<DataStream<TInput>> input,
                   std::shared_ptr<DataStream<TOutput>> output,
                   ProcessorFunction processor);

    void processSync() override;
    void processAsync() override;

    void setProcessor(ProcessorFunction processor);
    void setBufferSize(size_t size);

private:
    std::shared_ptr<DataStream<TInput>> input_;
    std::shared_ptr<DataStream<TOutput>> output_;
    ProcessorFunction processor_;
    size_t bufferSize_;
};

} // namespace axonvex::io::streams