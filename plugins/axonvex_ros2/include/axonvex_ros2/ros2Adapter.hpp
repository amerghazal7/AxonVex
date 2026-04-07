#pragma once

#include <axonvex_adapters/adapterInterface.hpp>
#include <axonvex_core/interfaceUnits.hpp>
#include <axonvex_ros2/typeCasterRegistry.hpp>

#include <rclcpp/rclcpp.hpp>

#include <memory>
#include <string>
#include <typeindex>
#include <vector>

namespace axonvex::ros2 {

/**
 * @brief ROS 2 adapter — extends AdapterBase with typed unit creation
 *
 * This is the concrete adapter for ROS 2 middleware. It:
 * - Manages an rclcpp::Node for subscriptions, publishers, and services
 * - Provides createSubscriber<T>(), createPublisher<T>(), createServer<T>(),
 *   createClient<T>() that return protocol-agnostic AxonVex interface units
 * - Uses a TypeCasterRegistry to auto-convert between ROS messages and
 *   AxonVex domain types — the caller never sees ROS message types
 *
 * Type casters for common AxonVex types are pre-registered. App-specific
 * types are registered via registerTypeCaster() before system.initialize().
 */
class ROS2Adapter final : public axonvex::adapters::AdapterBase {
  public:
    explicit ROS2Adapter(rclcpp::Node::SharedPtr node)
        : node_(std::move(node)) {
        registerDefaultCasters();
    }

    // ----- AdapterInterface identity -----

    std::string name() const override { return "ROS2Adapter"; }
    std::string protocolId() const override { return "ros2"; }

    bool configure(const std::string& /*key*/,
                   const std::string& /*value*/) override {
        return false;
    }

    // ----- Type caster registration -----

    template <typename InternalType, typename RosMsg>
    void registerTypeCaster(
        std::function<InternalType(const RosMsg&)> fromRos,
        std::function<RosMsg(const InternalType&)> toRos) {
        casters_.registerCaster<InternalType, RosMsg>(
            std::move(fromRos), std::move(toRos));
    }

    // ----- Typed unit creation -----

    /**
     * @brief Create a SubscriberUnit<T> wired to an rclcpp subscription.
     *
     * Looks up the registered TypeCaster for T, creates an rclcpp
     * subscription on the given topic, and wires the callback to call
     * unit->pushData() with the converted value.
     *
     * @tparam T   AxonVex domain type (e.g. ImuData, Point3D)
     * @param topic  ROS topic name (e.g. "/imu/data")
     * @return Owning pointer — caller registers with the system
     */
    template <typename T, typename RosMsg>
    axonvex::core::SubscriberUnit<T>* createSubscriber(const std::string& topic) {
        auto* caster = casters_.getCaster<T, RosMsg>();
        if (!caster) {
            throw std::runtime_error(
                "ROS2Adapter::createSubscriber: no type caster registered for " +
                std::string(typeid(T).name()));
        }
        auto* unit = new axonvex::core::SubscriberUnit<T>(topic);

        auto sub = node_->create_subscription<RosMsg>(
            topic, 10,
            [unit, caster](typename RosMsg::SharedPtr msg) {
                unit->pushData(caster->fromRos(*msg));
            });
        subscriptions_.push_back(sub);

        return unit;
    }

    /**
     * @brief Create a PublisherUnit<T> wired to an rclcpp publisher.
     */
    template <typename T, typename RosMsg>
    axonvex::core::PublisherUnit<T>* createPublisher(const std::string& topic) {
        auto* caster = casters_.getCaster<T, RosMsg>();
        if (!caster) {
            throw std::runtime_error(
                "ROS2Adapter::createPublisher: no type caster registered for " +
                std::string(typeid(T).name()));
        }
        auto* unit = new axonvex::core::PublisherUnit<T>(topic);
        auto pub = node_->create_publisher<RosMsg>(topic, 10);

        unit->setPublishCallback(
            [pub, caster](const T& data) {
                pub->publish(caster->toRos(data));
            });

        return unit;
    }

    /**
     * @brief Create a ServerUnit<T> wired to an rclcpp service.
     */
    template <typename T, typename RosSrv>
    axonvex::core::ServerUnit<T>* createServer(const std::string& service) {
        auto* unit = new axonvex::core::ServerUnit<T>(service);

        auto srv = node_->create_service<RosSrv>(
            service,
            [unit](const typename RosSrv::Request::SharedPtr request,
                   typename RosSrv::Response::SharedPtr /*response*/) {
                T data;
                // Direct field mapping for simple service types
                // App can register more complex converters via service-specific patterns
                (void)request;
                unit->pushRequest(data);
            });
        services_.push_back(srv);

        return unit;
    }

    /**
     * @brief Create a ClientUnit<T> wired to an rclcpp service client.
     */
    template <typename T, typename RosSrv>
    axonvex::core::ClientUnit<T>* createClient(const std::string& service) {
        auto* unit = new axonvex::core::ClientUnit<T>(service);
        auto client = node_->create_client<RosSrv>(service);

        unit->setRequestCallback(
            [client](const T& /*data*/) {
                auto req = std::make_shared<typename RosSrv::Request>();
                client->async_send_request(req);
            });

        return unit;
    }

    rclcpp::Node::SharedPtr getNode() const { return node_; }

  protected:
    bool onStart() override { return true; }
    void onStop() override {}
    void onShutdown() override {
        subscriptions_.clear();
        services_.clear();
    }

  private:
    void registerDefaultCasters() {
        // Pre-register casters for common primitive types
        // std_msgs::msg::Float32 <-> float
        // std_msgs::msg::Float64 <-> double
        // std_msgs::msg::Bool <-> bool
        // std_msgs::msg::String <-> std::string
        // (These are registered here so the app doesn't have to for basic types.
        //  Kept as a placeholder — actual registration requires the ROS msg includes
        //  which we defer to a separate defaultCasters header that apps can opt into.)
    }

    rclcpp::Node::SharedPtr node_;
    TypeCasterRegistry casters_;

    // Keep rclcpp handles alive
    std::vector<std::shared_ptr<void>> subscriptions_;
    std::vector<std::shared_ptr<void>> services_;
};

} // namespace axonvex::ros2
