/**
 * @file enhancedPortsTest.cpp
 * @brief Unit tests for Enhanced Port System
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/core/enhancedPorts.hpp>
#include <axonvex/core/enhancedProcessingUnit.hpp>
#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>

namespace axonvex::core::test {

// Test ProcessingUnit for creating ports
class TestEnhancedProcessingUnit : public EnhancedProcessingUnit {
public:
    explicit TestEnhancedProcessingUnit(const std::string& name) : EnhancedProcessingUnit(name) {}
    
    void processSyncDerived() override {
        syncCallCount_++;
    }
    
    void processAsyncDerived() override {
        asyncCallCount_++;
    }
    
    void reset() override {
        resetCallCount_++;
        syncCallCount_ = 0;
        asyncCallCount_ = 0;
    }
    
    std::string getTypeDescription() override {
        return "TestEnhancedProcessingUnit";
    }
    
    int getSyncCallCount() const { return syncCallCount_; }
    int getAsyncCallCount() const { return asyncCallCount_; }
    int getResetCallCount() const { return resetCallCount_; }
    
private:
    int syncCallCount_ = 0;
    int asyncCallCount_ = 0;
    int resetCallCount_ = 0;
};

// Tests for SyncInputPort
class SyncInputPortTest : public ::testing::Test {
protected:
    void SetUp() override {
        unit = std::make_unique<TestEnhancedProcessingUnit>("TestUnit");
        unit->setBlockUID(1);
        port = unit->createInputPort<int>(1, "TestInput");
    }
    
    std::unique_ptr<TestEnhancedProcessingUnit> unit;
    SyncInputPort<int>* port;
};

TEST_F(SyncInputPortTest, Construction) {
    EXPECT_EQ(port->getId(), 1);
    EXPECT_EQ(port->getName(), "TestInput");
    EXPECT_EQ(port->getType(), PortType::SYNC_INPUT);
    EXPECT_EQ(port->getOwner(), unit.get());
    EXPECT_EQ(port->getPortUID(), 1 * 256 + 1); // block_uid*256 + port_idx
}

TEST_F(SyncInputPortTest, DataWriteAndRead) {
    EXPECT_FALSE(port->hasNewData());
    
    port->writeData(42);
    EXPECT_TRUE(port->hasNewData());
    EXPECT_EQ(port->read(), 42);
    EXPECT_TRUE(port->hasNewData()); // Still has data until cleared
    
    port->clearNewDataFlag();
    EXPECT_FALSE(port->hasNewData());
}

TEST_F(SyncInputPortTest, ValidationCallback) {
    int validCount = 0;
    int invalidCount = 0;
    
    port->setValidationCallback([&](const int& data) -> bool {
        if (data > 0) {
            validCount++;
            return true;
        } else {
            invalidCount++;
            return false;
        }
    });
    
    port->writeData(10);  // Valid
    port->writeData(-5);  // Invalid
    port->writeData(20);  // Valid
    
    EXPECT_EQ(validCount, 2);
    EXPECT_EQ(invalidCount, 1);
    EXPECT_EQ(port->getValidMessages(), 2);
    EXPECT_EQ(port->getInvalidMessages(), 1);
    EXPECT_EQ(port->getTotalMessages(), 3);
}

TEST_F(SyncInputPortTest, DataCallback) {
    std::vector<int> receivedData;
    
    port->setDataCallback([&](const int& data) {
        receivedData.push_back(data);
    });
    
    port->writeData(1);
    port->writeData(2);
    port->writeData(3);
    
    EXPECT_EQ(receivedData.size(), 3);
    EXPECT_EQ(receivedData[0], 1);
    EXPECT_EQ(receivedData[1], 2);
    EXPECT_EQ(receivedData[2], 3);
}

TEST_F(SyncInputPortTest, BridgePortFunctionality) {
    auto unit2 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit2");
    unit2->setBlockUID(2);
    auto bridgedPort = unit2->createInputPort<int>(1, "BridgedInput");
    
    EXPECT_FALSE(port->isBridgePort());
    
    port->addBridgedPort(bridgedPort);
    EXPECT_TRUE(port->isBridgePort());
    
    // Writing to main port should propagate to bridged port
    port->writeData(100);
    EXPECT_TRUE(port->hasNewData());
    EXPECT_TRUE(bridgedPort->hasNewData());
    EXPECT_EQ(bridgedPort->read(), 100);
    
    port->removeBridgedPort(bridgedPort);
    EXPECT_FALSE(port->isBridgePort());
}

TEST_F(SyncInputPortTest, ThreadSafety) {
    port->setThreadSafe(true);
    EXPECT_TRUE(port->isThreadSafe());
    
    port->writeData(50);
    EXPECT_EQ(port->read(), 50);
    
    port->setThreadSafe(false);
    EXPECT_FALSE(port->isThreadSafe());
}

TEST_F(SyncInputPortTest, Reset) {
    port->writeData(123);
    EXPECT_TRUE(port->hasNewData());
    
    port->reset();
    EXPECT_FALSE(port->hasNewData());
    EXPECT_EQ(port->read(), 0); // Default initialized value
}

// Tests for SyncOutputPort
class SyncOutputPortTest : public ::testing::Test {
protected:
    void SetUp() override {
        unit = std::make_unique<TestEnhancedProcessingUnit>("TestUnit");
        unit->setBlockUID(1);
        port = unit->createOutputPort<int>(1, "TestOutput");
    }
    
    std::unique_ptr<TestEnhancedProcessingUnit> unit;
    SyncOutputPort<int>* port;
};

TEST_F(SyncOutputPortTest, Construction) {
    EXPECT_EQ(port->getId(), 1);
    EXPECT_EQ(port->getName(), "TestOutput");
    EXPECT_EQ(port->getType(), PortType::SYNC_OUTPUT);
    EXPECT_EQ(port->getOwner(), unit.get());
    EXPECT_FALSE(port->isConnected());
    EXPECT_EQ(port->getConnectionCount(), 0);
}

TEST_F(SyncOutputPortTest, Connection) {
    auto unit2 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit2");
    unit2->setBlockUID(2);
    auto inputPort = unit2->createInputPort<int>(1, "TestInput");
    
    port->connect(inputPort);
    EXPECT_TRUE(port->isConnected());
    EXPECT_EQ(port->getConnectionCount(), 1);
    
    // Writing to output should send to connected input
    port->write(75);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_EQ(inputPort->read(), 75);
}

TEST_F(SyncOutputPortTest, MultipleConnections) {
    auto unit2 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit2");
    auto unit3 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit3");
    unit2->setBlockUID(2);
    unit3->setBlockUID(3);
    
    auto inputPort1 = unit2->createInputPort<int>(1, "Input1");
    auto inputPort2 = unit3->createInputPort<int>(1, "Input2");
    
    port->connect(inputPort1);
    port->connect(inputPort2);
    EXPECT_EQ(port->getConnectionCount(), 2);
    
    // Writing to output should send to both inputs
    port->write(99);
    EXPECT_TRUE(inputPort1->hasNewData());
    EXPECT_TRUE(inputPort2->hasNewData());
    EXPECT_EQ(inputPort1->read(), 99);
    EXPECT_EQ(inputPort2->read(), 99);
}

TEST_F(SyncOutputPortTest, OutputCallback) {
    std::vector<int> outputData;
    
    port->setOutputCallback([&](const int& data) {
        outputData.push_back(data);
    });
    
    port->write(10);
    port->write(20);
    
    EXPECT_EQ(outputData.size(), 2);
    EXPECT_EQ(outputData[0], 10);
    EXPECT_EQ(outputData[1], 20);
}

TEST_F(SyncOutputPortTest, Disconnect) {
    auto unit2 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit2");
    unit2->setBlockUID(2);
    auto inputPort = unit2->createInputPort<int>(1, "TestInput");
    
    port->connect(inputPort);
    EXPECT_TRUE(port->isConnected());
    
    port->disconnect(inputPort);
    EXPECT_FALSE(port->isConnected());
    EXPECT_EQ(port->getConnectionCount(), 0);
    
    // Writing should not affect disconnected input
    port->write(123);
    EXPECT_FALSE(inputPort->hasNewData());
}

// Tests for AsyncInputPort
class AsyncInputPortTest : public ::testing::Test {
protected:
    void SetUp() override {
        unit = std::make_unique<TestEnhancedProcessingUnit>("TestUnit");
        unit->setBlockUID(1);
        port = unit->createAsyncInputPort<int>(1, "TestAsyncInput");
    }
    
    std::unique_ptr<TestEnhancedProcessingUnit> unit;
    AsyncInputPort<int>* port;
};

TEST_F(AsyncInputPortTest, Construction) {
    EXPECT_EQ(port->getId(), 1);
    EXPECT_EQ(port->getName(), "TestAsyncInput");
    EXPECT_EQ(port->getType(), PortType::ASYNC_INPUT);
    EXPECT_FALSE(port->wasUpdated_AsyncIP());
}

TEST_F(AsyncInputPortTest, AsyncUpdateAndRead) {
    EXPECT_FALSE(port->wasUpdated_AsyncIP());
    
    port->update_AsyncIP(42);
    EXPECT_TRUE(port->wasUpdated_AsyncIP());
    
    int data;
    port->read_AsyncIP(data);
    EXPECT_EQ(data, 42);
    EXPECT_FALSE(port->wasUpdated_AsyncIP()); // Flag cleared after read
}

TEST_F(AsyncInputPortTest, AsyncReadWithReturn) {
    port->update_AsyncIP(99);
    int data = port->read_AsyncIP();
    EXPECT_EQ(data, 99);
    EXPECT_FALSE(port->wasUpdated_AsyncIP());
}

TEST_F(AsyncInputPortTest, ReadWithoutUpdate) {
    EXPECT_THROW(port->read_AsyncIP(), std::runtime_error);
}

TEST_F(AsyncInputPortTest, AsyncBridgePorts) {
    auto unit2 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit2");
    unit2->setBlockUID(2);
    auto bridgedPort = unit2->createAsyncInputPort<int>(1, "BridgedAsyncInput");
    
    port->addBridgedPort(bridgedPort);
    EXPECT_TRUE(port->isBridgePort());
    
    // Update should propagate to bridged port
    port->update_AsyncIP(77);
    EXPECT_TRUE(port->wasUpdated_AsyncIP());
    EXPECT_TRUE(bridgedPort->wasUpdated_AsyncIP());
    
    int data1 = port->read_AsyncIP();
    int data2 = bridgedPort->read_AsyncIP();
    EXPECT_EQ(data1, 77);
    EXPECT_EQ(data2, 77);
}

// Tests for AsyncOutputPort
class AsyncOutputPortTest : public ::testing::Test {
protected:
    void SetUp() override {
        unit = std::make_unique<TestEnhancedProcessingUnit>("TestUnit");
        unit->setBlockUID(1);
        port = unit->createAsyncOutputPort<int>(1, "TestAsyncOutput");
    }
    
    std::unique_ptr<TestEnhancedProcessingUnit> unit;
    AsyncOutputPort<int>* port;
};

TEST_F(AsyncOutputPortTest, Construction) {
    EXPECT_EQ(port->getId(), 1);
    EXPECT_EQ(port->getName(), "TestAsyncOutput");
    EXPECT_EQ(port->getType(), PortType::ASYNC_OUTPUT);
    EXPECT_FALSE(port->isConnected());
    EXPECT_EQ(port->getConnectionCount(), 0);
}

TEST_F(AsyncOutputPortTest, AsyncConnection) {
    auto unit2 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit2");
    unit2->setBlockUID(2);
    auto inputPort = unit2->createAsyncInputPort<int>(1, "TestAsyncInput");
    
    port->connectAsync(inputPort);
    EXPECT_TRUE(port->isConnected());
    EXPECT_EQ(port->getConnectionCount(), 1);
    
    // Writing should trigger update on connected input
    port->write_AsyncOP(55);
    EXPECT_TRUE(inputPort->wasUpdated_AsyncIP());
    EXPECT_EQ(inputPort->read_AsyncIP(), 55);
}

TEST_F(AsyncOutputPortTest, MultipleAsyncConnections) {
    auto unit2 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit2");
    auto unit3 = std::make_unique<TestEnhancedProcessingUnit>("TestUnit3");
    unit2->setBlockUID(2);
    unit3->setBlockUID(3);
    
    auto inputPort1 = unit2->createAsyncInputPort<int>(1, "Input1");
    auto inputPort2 = unit3->createAsyncInputPort<int>(1, "Input2");
    
    port->connectAsync(inputPort1);
    port->connectAsync(inputPort2);
    EXPECT_EQ(port->getConnectionCount(), 2);
    
    // Writing should update both inputs
    port->write_AsyncOP(88);
    EXPECT_TRUE(inputPort1->wasUpdated_AsyncIP());
    EXPECT_TRUE(inputPort2->wasUpdated_AsyncIP());
    EXPECT_EQ(inputPort1->read_AsyncIP(), 88);
    EXPECT_EQ(inputPort2->read_AsyncIP(), 88);
}

// Tests for NaN validation
class NaNValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        unit = std::make_unique<TestEnhancedProcessingUnit>("TestUnit");
        unit->setBlockUID(1);
        floatPort = unit->createInputPort<float>(1, "FloatInput");
    }
    
    std::unique_ptr<TestEnhancedProcessingUnit> unit;
    SyncInputPort<float>* floatPort;
};

TEST_F(NaNValidationTest, NaNDetection) {
    // Valid data should be accepted
    floatPort->writeData(3.14f);
    EXPECT_EQ(floatPort->getValidMessages(), 1);
    EXPECT_EQ(floatPort->getInvalidMessages(), 0);
    
    // NaN should be rejected
    floatPort->writeData(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(floatPort->getValidMessages(), 1);
    EXPECT_EQ(floatPort->getInvalidMessages(), 1);
    
    // More valid data should be accepted
    floatPort->writeData(2.71f);
    EXPECT_EQ(floatPort->getValidMessages(), 2);
    EXPECT_EQ(floatPort->getInvalidMessages(), 1);
}

// Integration tests
class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        producer = std::make_unique<TestEnhancedProcessingUnit>("Producer");
        processor = std::make_unique<TestEnhancedProcessingUnit>("Processor");
        consumer = std::make_unique<TestEnhancedProcessingUnit>("Consumer");
        
        producer->setBlockUID(1);
        processor->setBlockUID(2);
        consumer->setBlockUID(3);
        
        producerOutput = producer->createOutputPort<int>(1, "Data");
        processorInput = processor->createInputPort<int>(1, "Input");
        processorOutput = processor->createOutputPort<int>(1, "Output");
        consumerInput = consumer->createInputPort<int>(1, "Input");
        
        // Connect the pipeline
        producerOutput->connect(processorInput);
        processorOutput->connect(consumerInput);
    }
    
    std::unique_ptr<TestEnhancedProcessingUnit> producer;
    std::unique_ptr<TestEnhancedProcessingUnit> processor;
    std::unique_ptr<TestEnhancedProcessingUnit> consumer;
    
    SyncOutputPort<int>* producerOutput;
    SyncInputPort<int>* processorInput;
    SyncOutputPort<int>* processorOutput;
    SyncInputPort<int>* consumerInput;
};

TEST_F(IntegrationTest, DataFlowPipeline) {
    // Producer sends data
    producerOutput->write(10);
    EXPECT_TRUE(processorInput->hasNewData());
    
    // Processor reads, processes, and outputs
    int inputData = processorInput->read();
    processorInput->clearNewDataFlag();
    processorOutput->write(inputData * 2);
    
    // Consumer receives processed data
    EXPECT_TRUE(consumerInput->hasNewData());
    EXPECT_EQ(consumerInput->read(), 20);
}

TEST_F(IntegrationTest, PortUIDGeneration) {
    // Verify port UIDs follow formula: block_uid*256 + port_idx
    EXPECT_EQ(producerOutput->getPortUID(), 1 * 256 + 1); // Block 1, Port 1
    EXPECT_EQ(processorInput->getPortUID(), 2 * 256 + 1);  // Block 2, Port 1
    EXPECT_EQ(processorOutput->getPortUID(), 2 * 256 + 1); // Block 2, Port 1
    EXPECT_EQ(consumerInput->getPortUID(), 3 * 256 + 1);   // Block 3, Port 1
}

} // namespace axonvex::core::test 