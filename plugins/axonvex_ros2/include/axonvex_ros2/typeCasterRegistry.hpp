#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace axonvex::ros2 {

/**
 * @brief Type-erased base for subscription/publisher wiring functions.
 *
 * Each registered type caster stores lambdas that know how to:
 * - Convert a ROS message into an internal type (for subscribers)
 * - Convert an internal type into a ROS message (for publishers)
 * - Wire an rclcpp subscription/publisher to a SubscriberUnit/PublisherUnit
 *
 * The concrete type is hidden behind this base so the registry can
 * store heterogeneous casters in a single map keyed by typeid.
 */
class TypeCasterBase {
  public:
    virtual ~TypeCasterBase() = default;
};

/**
 * @brief Concrete type caster for a specific InternalType <-> RosMsg pair.
 */
template <typename InternalType, typename RosMsg>
class TypeCaster : public TypeCasterBase {
  public:
    using FromRosFn = std::function<InternalType(const RosMsg&)>;
    using ToRosFn   = std::function<RosMsg(const InternalType&)>;

    TypeCaster(FromRosFn from, ToRosFn to)
        : fromRos_(std::move(from)), toRos_(std::move(to)) {}

    InternalType fromRos(const RosMsg& msg) const { return fromRos_(msg); }
    RosMsg toRos(const InternalType& data) const { return toRos_(data); }

  private:
    FromRosFn fromRos_;
    ToRosFn toRos_;
};

/**
 * @brief Registry mapping typeid(InternalType) -> TypeCasterBase.
 */
class TypeCasterRegistry {
  public:
    template <typename InternalType, typename RosMsg>
    void registerCaster(
        typename TypeCaster<InternalType, RosMsg>::FromRosFn fromRos,
        typename TypeCaster<InternalType, RosMsg>::ToRosFn toRos) {
        casters_[std::type_index(typeid(InternalType))] =
            std::make_unique<TypeCaster<InternalType, RosMsg>>(
                std::move(fromRos), std::move(toRos));
    }

    template <typename InternalType, typename RosMsg>
    const TypeCaster<InternalType, RosMsg>* getCaster() const {
        auto it = casters_.find(std::type_index(typeid(InternalType)));
        if (it == casters_.end()) return nullptr;
        return dynamic_cast<const TypeCaster<InternalType, RosMsg>*>(
            it->second.get());
    }

    bool hasCaster(std::type_index ti) const {
        return casters_.count(ti) > 0;
    }

    TypeCasterBase* getRawCaster(std::type_index ti) const {
        auto it = casters_.find(ti);
        return it != casters_.end() ? it->second.get() : nullptr;
    }

  private:
    std::unordered_map<std::type_index, std::unique_ptr<TypeCasterBase>> casters_;
};

} // namespace axonvex::ros2
