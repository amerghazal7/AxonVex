/**
 * @file main.cpp
 * @brief AxonVex ROS 2 Bridge — String echo demo
 *
 * Subscribes to /chatter (std_msgs/String), pipes through AxonVex pipeline,
 * logs every message to console.
 */

#include <axonvex_core/axonvex.hpp>
#include <axonvex_ros2/ros2Adapter.hpp>
#include <iostream>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>

using namespace axonvex::core;

// =====================================================================
// Processing Unit — just prints whatever arrives on its input port
// =====================================================================

class PrinterPU : public ProcessingUnit {
  public:
    explicit PrinterPU(const std::string& name) : ProcessingUnit(name) {
        in_ = createInputPort<std::string>(0, "input");
    }
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }
    void processSync() override {
        setState(ExecutionState::RUNNING);
        if (!in_->hasNewData())
            return;
        auto msg = in_->read();
        in_->clearNewDataFlag();
        count_++;
        std::cout << "[PrinterPU] #" << count_ << ": " << msg << std::endl;
    }
    void processAsync() override {}
    void reset() override {
        count_ = 0;
        setState(ExecutionState::INITIALIZED);
    }
    std::string getTypeDescription() override {
        return "PrinterPU";
    }
    uint64_t getCount() const {
        return count_;
    }

  private:
    InputPort<std::string>* in_;
    uint64_t count_{0};
};

// =====================================================================
// System — subscribes to /chatter, pipes to PrinterPU
// =====================================================================

class EchoSystem : public AxonVexSystem {
  public:
    explicit EchoSystem(const SystemConfiguration& cfg) : AxonVexSystem(cfg) {}
    PrinterPU* getPrinter() const {
        return printer_;
    }

  protected:
    bool initializeBlocksLayout() override {
        auto* ros = static_cast<axonvex::ros2::ROS2Adapter*>(getAdapter("ros"));
        if (!ros)
            return false;

        auto sub = ros->createSubscriber<std::string, std_msgs::msg::String>("/chatter");
        auto* subPtr = sub.get();
        registerProcessingUnit(std::move(sub));

        auto printer = std::make_unique<PrinterPU>("Printer");
        printer_ = printer.get();
        registerProcessingUnit(std::move(printer));

        subPtr->getOutput()->connect(printer_->getInputPort<std::string>(0));
        std::cout << "[System] /chatter -> PrinterPU" << std::endl;
        return true;
    }

  private:
    PrinterPU* printer_{nullptr};
};

// =====================================================================
// main
// =====================================================================

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("axonvex_bridge");

    std::cout << "=== AxonVex String Echo Demo ===" << std::endl;

    auto adapter = std::make_unique<axonvex::ros2::ROS2Adapter>(node);

    adapter->registerTypeCaster<std::string, std_msgs::msg::String>(
        [](const std_msgs::msg::String& msg) -> std::string { return msg.data; },
        [](const std::string& s) -> std_msgs::msg::String {
            std_msgs::msg::String m;
            m.data = s;
            return m;
        });

    adapter->initialize();
    adapter->start();

    SystemConfiguration cfg;
    cfg.systemName = "EchoDemo";
    cfg.enableRealTimeScheduling = false;
    cfg.enableFileLogging = false;

    EchoSystem system(cfg);
    system.addAdapter(adapter.get(), "ros");

    if (!system.initialize() || !system.start()) {
        std::cerr << "Failed to start" << std::endl;
        return 1;
    }

    std::cout << "Listening on /chatter. In another terminal run:\n"
              << "  ros2 topic pub /chatter std_msgs/msg/String \"data: 'hello axonvex'\"\n"
              << std::endl;

    rclcpp::spin(node);

    system.stop();
    adapter->stop();

    std::cout << "\nMessages received: "
              << (system.getPrinter() ? system.getPrinter()->getCount() : 0) << std::endl;

    rclcpp::shutdown();
    return 0;
}
