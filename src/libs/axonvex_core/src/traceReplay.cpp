#include <axonvex_core/replay/traceReplay.hpp>
#include <fstream>
#include <sstream>

namespace axonvex::core::replay {

namespace {

std::string stateString(SystemState s) {
    return to_string(s);
}

} // namespace

bool TraceRecordEntry::operator==(const TraceRecordEntry& o) const {
    return sequence == o.sequence && relativeNanos == o.relativeNanos && type == o.type &&
           oldState == o.oldState && newState == o.newState && description == o.description &&
           metadata == o.metadata;
}

std::string TraceRecorder::typeString(SystemEvent::Type t) {
    switch (t) {
        case SystemEvent::Type::STATE_CHANGE:
            return "STATE_CHANGE";
        case SystemEvent::Type::PROCESSING_UNIT_ADDED:
            return "PROCESSING_UNIT_ADDED";
        case SystemEvent::Type::PROCESSING_UNIT_REMOVED:
            return "PROCESSING_UNIT_REMOVED";
        case SystemEvent::Type::PROCESSING_UNIT_ERROR:
            return "PROCESSING_UNIT_ERROR";
        case SystemEvent::Type::PERFORMANCE_ALERT:
            return "PERFORMANCE_ALERT";
        case SystemEvent::Type::CONFIGURATION_CHANGED:
            return "CONFIGURATION_CHANGED";
        case SystemEvent::Type::RECOVERY_STARTED:
            return "RECOVERY_STARTED";
        case SystemEvent::Type::RECOVERY_COMPLETED:
            return "RECOVERY_COMPLETED";
        case SystemEvent::Type::HEALTH_CHECK:
            return "HEALTH_CHECK";
        case SystemEvent::Type::SHUTDOWN_REQUESTED:
            return "SHUTDOWN_REQUESTED";
        default:
            return "UNKNOWN";
    }
}

TraceRecorder::TraceRecorder() = default;

void TraceRecorder::record(const SystemEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasT0_) {
        t0_ = event.timestamp;
        hasT0_ = true;
    }

    TraceRecordEntry e;
    e.sequence = nextSeq_++;
    auto rel = std::chrono::duration_cast<std::chrono::nanoseconds>(event.timestamp - t0_);
    e.relativeNanos = rel.count();
    e.type = typeString(event.type);
    e.oldState = stateString(event.oldState);
    e.newState = stateString(event.newState);
    e.description = event.description;
    e.metadata = nlohmann::json::object();
    for (const auto& kv : event.metadata) {
        e.metadata[kv.first] = kv.second;
    }
    entries_.push_back(std::move(e));
}

void TraceRecorder::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    nextSeq_ = 0;
    hasT0_ = false;
}

size_t TraceRecorder::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

bool TraceRecorder::saveToFile(const Path& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : entries_) {
            nlohmann::json o;
            o["sequence"] = e.sequence;
            o["relativeNanos"] = e.relativeNanos;
            o["type"] = e.type;
            o["oldState"] = e.oldState;
            o["newState"] = e.newState;
            o["description"] = e.description;
            o["metadata"] = e.metadata;
            arr.push_back(o);
        }
        path.parent().createDirectories();
        std::ofstream ofs(path.native().string(), std::ios::binary);
        if (!ofs)
            return false;
        ofs << arr.dump(2);
        return ofs.good();
    } catch (...) { return false; }
}

bool TraceReplayer::loadFromFile(const Path& path) {
    entries_.clear();
    try {
        std::ifstream ifs(path.native().string(), std::ios::binary);
        if (!ifs)
            return false;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        nlohmann::json arr = nlohmann::json::parse(ss.str());
        if (!arr.is_array())
            return false;
        for (const auto& o : arr) {
            TraceRecordEntry e;
            e.sequence = o.at("sequence").get<uint64_t>();
            e.relativeNanos = o.at("relativeNanos").get<int64_t>();
            e.type = o.at("type").get<std::string>();
            e.oldState = o.at("oldState").get<std::string>();
            e.newState = o.at("newState").get<std::string>();
            e.description = o.at("description").get<std::string>();
            e.metadata = o.at("metadata");
            entries_.push_back(std::move(e));
        }
        return true;
    } catch (...) {
        entries_.clear();
        return false;
    }
}

void TraceReplayer::replay(const std::function<void(const TraceRecordEntry&)>& callback) const {
    if (!callback)
        return;
    for (const auto& e : entries_) {
        callback(e);
    }
}

bool TraceReplayer::sequencesMatch(const TraceRecorder& recorded, const TraceReplayer& loaded) {
    const auto& a = recorded.entries();
    const auto& b = loaded.entries();
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i]))
            return false;
    }
    return true;
}

} // namespace axonvex::core::replay
