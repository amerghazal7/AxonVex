/**
 * @file simple_callback_demo.cpp
 * @brief Simple callback system demonstration
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 * 
 * A concise demonstration of the AxonVex callback system functionality.
 */

#include <axonvex/axonvex.hpp>
#include <iostream>
#include <string>

// Simple data processor
class DataProcessor : public axonvex::core::Callback<int> {
public:
    void callbackPerform(const int data) override {
        std::cout << "Processing data: " << data << " -> result: " << (data * 2) << std::endl;
    }
};

// Simple logger
class Logger : public axonvex::core::Callback<int> {
public:
    void callbackPerform(const int data) override {
        std::cout << "Log: Received value " << data << std::endl;
    }
};

// String processor for keyed demo
class MessageHandler : public axonvex::core::Callback<std::string> {
public:
    void callbackPerform(const std::string data) override {
        std::cout << "Handling message: " << data << std::endl;
    }
};

int main() {
    std::cout << "AxonVex Callback System - Simple Demo\n" << std::endl;
    
    // === Basic Caller Demo ===
    std::cout << "1. Basic Caller Demo:" << std::endl;
    
    DataProcessor processor;
    Logger logger;
    axonvex::core::Caller<int> caller;
    
    caller.registerCallback(&processor);
    caller.registerCallback(&logger);
    
    std::cout << "Triggering callbacks with value 42:" << std::endl;
    caller.callCallbacks(42);
    
    std::cout << "\n2. Keyed Caller Demo:" << std::endl;
    
    MessageHandler handler;
    axonvex::core::CallerKeyed<std::string, std::string> keyedCaller;
    
    keyedCaller.registerKeyedCallback("INFO", &handler);
    keyedCaller.registerKeyedCallback("ERROR", &handler);
    
    std::cout << "Triggering INFO callback:" << std::endl;
    keyedCaller.callCallbacksByKey("INFO", "System startup complete");
    
    std::cout << "Triggering ERROR callback:" << std::endl;
    keyedCaller.callCallbacksByKey("ERROR", "Network connection failed");
    
    std::cout << "\n3. Integration with AxonVex features:" << std::endl;
    axonvex::printWelcome();
    
    std::cout << "\nCallback system successfully integrated!" << std::endl;
    return 0;
} 