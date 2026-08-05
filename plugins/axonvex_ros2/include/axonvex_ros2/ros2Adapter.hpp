#pragma once

#include <axonvex_adapters/adapterInterface.hpp>
#include <axonvex_core/interfaceUnits.hpp>
#include <axonvex_ros2/typeCasterRegistry.hpp>

#ifdef AXONVEX_ROS2_HAS_STD_MSGS
#include <axonvex_ros2/defaultCasters.hpp>
#endif

#include <functional>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
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
 * Type casters for the four std_msgs primitives (float/double/bool/string)
 * are pre-registered when the plugin was built with std_msgs available
 * (AXONVEX_ROS2_HAS_STD_MSGS) — see registerDefaultCasters(). All other
 * types are registered via registerTypeCaster() before system.initialize().
 */
class ROS2Adapter final : public axonvex::adapters::AdapterBase {
  public:
    explicit ROS2Adapter(rclcpp::Node::SharedPtr node) : node_(std::move(node)) {
#ifdef AXONVEX_ROS2_HAS_STD_MSGS
        registerDefaultCasters();
#endif
    }

    // ----- AdapterInterface identity -----

    std::string name() const override {
        return "ROS2Adapter";
    }
    std::string protocolId() const override {
        return "ros2";
    }

    bool configure(const std::string& /*key*/, const std::string& /*value*/) override {
        return false;
    }

    // ----- Type caster registration -----

    template <typename InternalType, typename RosMsg>
    void registerTypeCaster(std::function<InternalType(const RosMsg&)> fromRos,
                            std::function<RosMsg(const InternalType&)> toRos) {
        casters_.registerCaster<InternalType, RosMsg>(std::move(fromRos), std::move(toRos));
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
     * @return Owning pointer. The subscription callback holds a raw
     *         pointer into the returned unit, so it must outlive this
     *         ROS2Adapter (register it with the system for the adapter's
     *         lifetime, same contract as before this returned a raw
     *         pointer — just explicit now instead of leaking on discard).
     */
    template <typename T, typename RosMsg>
    std::unique_ptr<axonvex::core::SubscriberUnit<T>> createSubscriber(const std::string& topic) {
        auto* caster = casters_.getCaster<T, RosMsg>();
        if (!caster) {
            throw std::runtime_error(
                "ROS2Adapter::createSubscriber: no type caster registered for InternalType=" +
                std::string(typeid(T).name()) + ", RosMsg=" + std::string(typeid(RosMsg).name()));
        }
        auto unit = std::make_unique<axonvex::core::SubscriberUnit<T>>(topic);
        auto* rawUnit = unit.get();

        auto sub = node_->create_subscription<RosMsg>(
            topic, 10, [rawUnit, caster](typename RosMsg::SharedPtr msg) {
                rawUnit->pushData(caster->fromRos(*msg));
            });
        subscriptions_.push_back(sub);

        return unit;
    }

    /**
     * @brief Create a PublisherUnit<T> wired to an rclcpp publisher.
     *
     * @return Owning pointer — the publish callback only captures the
     *         rclcpp publisher handle (kept alive by the shared_ptr), not
     *         the unit itself, so unlike createSubscriber() there is no
     *         lifetime coupling back to this adapter.
     */
    template <typename T, typename RosMsg>
    std::unique_ptr<axonvex::core::PublisherUnit<T>> createPublisher(const std::string& topic) {
        auto* caster = casters_.getCaster<T, RosMsg>();
        if (!caster) {
            throw std::runtime_error(
                "ROS2Adapter::createPublisher: no type caster registered for InternalType=" +
                std::string(typeid(T).name()) + ", RosMsg=" + std::string(typeid(RosMsg).name()));
        }
        auto unit = std::make_unique<axonvex::core::PublisherUnit<T>>(topic);
        auto pub = node_->create_publisher<RosMsg>(topic, 10);

        unit->setPublishCallback(
            [pub, caster](const T& data) { pub->publish(caster->toRos(data)); });

        return unit;
    }

    /**
     * @brief Create a ServerUnit<T> wired to an rclcpp service.
     *
     * @param requestMapper  Required. Converts the incoming
     *   RosSrv::Request into the T pushed through the unit's ports —
     *   without it the request payload would be silently discarded
     *   (the original defect). Must not be null.
     *
     * Response handling: axonvex::core::ServerUnit<T> (see
     * interfaceUnits.hpp) has no response path at all — pushRequest()
     * only fans the mapped request out to output ports for the pipeline to
     * read; there is no callback or port that feeds a value back into this
     * call. So every ROS service call handled here completes with a
     * default-constructed RosSrv::Response, regardless of what the
     * pipeline does with the request. This is honest current behavior, not
     * a placeholder for a response-mapper parameter: adding one would be
     * speculative until ServerUnit itself grows a response port. Apps
     * needing a real reply must drive it out-of-band (e.g. a separate
     * publisher/topic) until then.
     */
    template <typename T, typename RosSrv>
    std::unique_ptr<axonvex::core::ServerUnit<T>> createServer(
        const std::string& service,
        std::function<T(const typename RosSrv::Request&)> requestMapper) {
        if (!requestMapper) {
            throw std::invalid_argument(
                "ROS2Adapter::createServer: requestMapper must not be null for service " + service);
        }
        auto unit = std::make_unique<axonvex::core::ServerUnit<T>>(service);
        auto* rawUnit = unit.get();

        auto srv = node_->create_service<RosSrv>(
            service, [rawUnit, requestMapper](const typename RosSrv::Request::SharedPtr request,
                                              typename RosSrv::Response::SharedPtr /*response*/) {
                rawUnit->pushRequest(requestMapper(*request));
            });
        services_.push_back(srv);

        return unit;
    }

    /**
     * @brief Create a ClientUnit<T> wired to an rclcpp service client.
     */
    template <typename T, typename RosSrv>
    std::unique_ptr<axonvex::core::ClientUnit<T>> createClient(const std::string& service) {
        auto unit = std::make_unique<axonvex::core::ClientUnit<T>>(service);
        auto client = node_->create_client<RosSrv>(service);

        unit->setRequestCallback([client](const T& /*data*/) {
            auto req = std::make_shared<typename RosSrv::Request>();
            client->async_send_request(req);
        });

        return unit;
    }

    rclcpp::Node::SharedPtr getNode() const {
        return node_;
    }

  protected:
    bool onStart() override {
        return true;
    }
    void onStop() override {}
    void onShutdown() override {
        subscriptions_.clear();
        services_.clear();
    }

  private:
#ifdef AXONVEX_ROS2_HAS_STD_MSGS
    // Only exists when the plugin was built with std_msgs found — see
    // axonvex_ros2/CMakeLists.txt. Without std_msgs there is nothing to
    // register, so the method itself does not exist rather than compiling
    // to an empty body that claims to have registered casters it didn't
    // (the original C20 defect).
    void registerDefaultCasters() {
        axonvex::ros2::registerDefaultCasters(casters_);
    }
#endif

    rclcpp::Node::SharedPtr node_;
    TypeCasterRegistry casters_;

    // Keep rclcpp handles alive
    std::vector<std::shared_ptr<void>> subscriptions_;
    std::vector<std::shared_ptr<void>> services_;
};

} // namespace axonvex::ros2
