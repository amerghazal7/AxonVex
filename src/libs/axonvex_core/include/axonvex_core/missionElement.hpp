#pragma once

#include <string>

namespace axonvex::core {

/**
 * @brief Result returned by MissionElement::execute() each tick
 */
enum class TransitionResult { Awaiting, Default, Option1, Option2, Option3, Failed };

inline std::string transitionResultToString(TransitionResult r) {
    switch (r) {
        case TransitionResult::Awaiting:
            return "Awaiting";
        case TransitionResult::Default:
            return "Default";
        case TransitionResult::Option1:
            return "Option1";
        case TransitionResult::Option2:
            return "Option2";
        case TransitionResult::Option3:
            return "Option3";
        case TransitionResult::Failed:
            return "Failed";
    }
    return "Unknown";
}

/**
 * @brief Abstract base for a single mission step
 *
 * A MissionElement represents one node in a mission graph. The pipeline
 * calls execute() every tick while this element is current. The element
 * returns a TransitionResult to indicate whether to stay (Awaiting),
 * advance (Default/Option*), or signal failure (Failed).
 */
class MissionElement {
  public:
    virtual ~MissionElement() = default;

    explicit MissionElement(const std::string& name) : name_(name) {}

    /**
     * @brief Called every tick while this element is the current node.
     * @return TransitionResult indicating how to proceed.
     */
    virtual TransitionResult execute() = 0;

    /**
     * @brief Called once when the execution pointer enters this element.
     */
    virtual void onEnter() {}

    /**
     * @brief Called once when the execution pointer leaves this element.
     */
    virtual void onExit() {}

    /**
     * @brief Reset to initial state (called on pipeline restart).
     */
    virtual void reset() {}

    const std::string& name() const {
        return name_;
    }

  private:
    std::string name_;
};

} // namespace axonvex::core
