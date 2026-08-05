#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

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
 * store heterogeneous casters in a single map keyed by (InternalType, RosMsg).
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
    using ToRosFn = std::function<RosMsg(const InternalType&)>;

    TypeCaster(FromRosFn from, ToRosFn to) : fromRos_(std::move(from)), toRos_(std::move(to)) {}

    InternalType fromRos(const RosMsg& msg) const {
        return fromRos_(msg);
    }
    RosMsg toRos(const InternalType& data) const {
        return toRos_(data);
    }

  private:
    FromRosFn fromRos_;
    ToRosFn toRos_;
};

namespace detail {

/**
 * @brief Hash for a (InternalType, RosMsg) type_index pair.
 *
 * C++14 has no std::hash<std::pair<...>> specialization — this is the
 * standard two-value combine (boost::hash_combine's formula) rather than
 * pulling in a dependency for four lines of code.
 */
struct TypeIndexPairHash {
    std::size_t operator()(const std::pair<std::type_index, std::type_index>& p) const noexcept {
        std::size_t seed = std::hash<std::type_index>{}(p.first);
        seed ^= std::hash<std::type_index>{}(p.second) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
        return seed;
    }
};

} // namespace detail

/**
 * @brief Registry mapping (typeid(InternalType), typeid(RosMsg)) -> TypeCasterBase.
 *
 * Keyed by the exact pair so a single InternalType (e.g. float) can have
 * distinct casters registered against multiple RosMsg types (e.g. both
 * std_msgs::msg::Float32 and a custom message) without one overwriting the
 * other. Lookups are exact-pair: getCaster<T, Msg>() only ever returns the
 * caster registered for that exact (T, Msg) combination, or nullptr.
 */
class TypeCasterRegistry {
  public:
    using CasterKey = std::pair<std::type_index, std::type_index>;

    template <typename InternalType, typename RosMsg>
    void registerCaster(typename TypeCaster<InternalType, RosMsg>::FromRosFn fromRos,
                        typename TypeCaster<InternalType, RosMsg>::ToRosFn toRos) {
        casters_[keyFor<InternalType, RosMsg>()] =
            std::make_unique<TypeCaster<InternalType, RosMsg>>(std::move(fromRos),
                                                               std::move(toRos));
    }

    template <typename InternalType, typename RosMsg>
    const TypeCaster<InternalType, RosMsg>* getCaster() const {
        auto it = casters_.find(keyFor<InternalType, RosMsg>());
        if (it == casters_.end())
            return nullptr;
        return dynamic_cast<const TypeCaster<InternalType, RosMsg>*>(it->second.get());
    }

    template <typename InternalType, typename RosMsg>
    bool hasCaster() const {
        return casters_.count(keyFor<InternalType, RosMsg>()) > 0;
    }

    template <typename InternalType, typename RosMsg>
    TypeCasterBase* getRawCaster() const {
        auto it = casters_.find(keyFor<InternalType, RosMsg>());
        return it != casters_.end() ? it->second.get() : nullptr;
    }

  private:
    template <typename InternalType, typename RosMsg>
    static CasterKey keyFor() {
        return CasterKey(std::type_index(typeid(InternalType)), std::type_index(typeid(RosMsg)));
    }

    std::unordered_map<CasterKey, std::unique_ptr<TypeCasterBase>, detail::TypeIndexPairHash>
        casters_;
};

} // namespace axonvex::ros2
