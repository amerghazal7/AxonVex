#pragma once

#include <axonvex_core/path.hpp>
#include <axonvex_core/system.hpp>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>

namespace axonvex::core::replay {

struct TraceRecordEntry {
    uint64_t sequence{0};
    int64_t relativeNanos{0};
    std::string type;
    std::string oldState;
    std::string newState;
    std::string description;
    nlohmann::json metadata;

    bool operator==(const TraceRecordEntry& o) const;
};

/**
 * Records SystemEvent instances in deterministic order for regression replay.
 * Thread-safe; intended to be registered as an AxonVexSystem event callback.
 */
class TraceRecorder {
  public:
    TraceRecorder();

    void record(const SystemEvent& event);
    void clear();
    size_t size() const;

    bool saveToFile(const Path& path) const;
    const std::vector<TraceRecordEntry>& entries() const {
        return entries_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<TraceRecordEntry> entries_;
    uint64_t nextSeq_{0};
    std::chrono::steady_clock::time_point t0_;
    bool hasT0_{false};

    static std::string typeString(SystemEvent::Type t);
};

/**
 * Loads trace files produced by TraceRecorder and replays entries in order.
 */
class TraceReplayer {
  public:
    bool loadFromFile(const Path& path);
    void replay(const std::function<void(const TraceRecordEntry&)>& callback) const;
    const std::vector<TraceRecordEntry>& entries() const {
        return entries_;
    }

    static bool sequencesMatch(const TraceRecorder& recorded, const TraceReplayer& loaded);

  private:
    std::vector<TraceRecordEntry> entries_;
};

} // namespace axonvex::core::replay
