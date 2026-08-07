#include <axonvex_visualization/telemetryBus.hpp>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using axonvex::visualization::TelemetryBus;
using axonvex::visualization::TelemetryFrame;

// Subscriber callbacks run on the bus-owned egress thread.
class TelemetryPrinter : public TelemetryBus::Subscriber {
  public:
    explicit TelemetryPrinter(std::string name) : name_(std::move(name)) {}

    void callbackPerform(const TelemetryFrame frame) override {
        std::cout << "[" << name_ << "] " << frame.json << std::endl;
    }

  private:
    std::string name_;
};

int main() {
    TelemetryBus bus;

    TelemetryPrinter printerA("temp");
    TelemetryPrinter printerWildcard("wildcard");

    // Subscribe to a specific channel and to the wildcard channel.
    bus.subscribe("sensors/temp", &printerA);
    bus.subscribe("*", &printerWildcard);

    bus.start();

    // publish() is RT-safe: bounded, non-blocking; full queue drops the frame.
    bus.publish("sensors/temp", std::vector<uint8_t>{0x01, 0x02, 0x03});
    bus.publish("sensors/press", std::vector<uint8_t>{0x10, 0x20});

    // Unsubscribe and publish again — only the wildcard printer sees this one.
    bus.unsubscribe("sensors/temp", &printerA);
    bus.publish("sensors/temp", std::vector<uint8_t>{0x0A});

    bus.stop(); // drains every accepted frame before returning

    std::cout << "dropped: " << bus.droppedCount() << std::endl;
    return 0;
}
