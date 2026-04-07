#include <axonvex_core/axonvex.hpp>
#include <axonvex_visualization/telemetryBus.hpp>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using axonvex::visualization::TelemetryBus;

class TelemetryPrinter : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    void callbackPerform(const std::vector<uint8_t> data) override {
        std::cout << "[TelemetryPrinter] size=" << data.size() << ": ";
        for (auto b : data) std::cout << std::hex << static_cast<int>(b) << " ";
        std::cout << std::dec << std::endl;
    }
};

int main() {
    TelemetryBus bus;

    TelemetryPrinter printerA;
    TelemetryPrinter printerWildcard;

    // Subscribe to a specific channel and to wildcard
    bus.subscribe("sensors/temp", &printerA);
    bus.subscribe("*", &printerWildcard);

    // Publish some frames
    bus.publish("sensors/temp", std::vector<uint8_t>{0x01, 0x02, 0x03});
    bus.publish("sensors/press", std::vector<uint8_t>{0x10, 0x20});

    // Unsubscribe and publish again
    bus.unsubscribe("sensors/temp", &printerA);
    bus.publish("sensors/temp", std::vector<uint8_t>{0x0A});

    return 0;
}
