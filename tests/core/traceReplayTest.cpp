#include <axonvex_core/detail/filesystem_compat.hpp>
#include <axonvex_core/replay/traceReplay.hpp>
#include <axonvex_core/system.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>

using axonvex::core::SystemEvent;
using axonvex::core::SystemState;
using axonvex::core::replay::TraceRecorder;
using axonvex::core::replay::TraceReplayer;

namespace {

void makeEvent(SystemEvent::Type type, SystemState oldS, SystemState newS, const char* desc,
               TraceRecorder& rec) {
    SystemEvent e;
    e.type = type;
    e.oldState = oldS;
    e.newState = newS;
    e.description = desc;
    e.timestamp = std::chrono::steady_clock::now();
    e.metadata["k"] = "v";
    rec.record(e);
}

} // namespace

TEST(TraceReplayTest, RecordAndRoundTripFile) {
    TraceRecorder rec;
    makeEvent(SystemEvent::Type::STATE_CHANGE, SystemState::UNINITIALIZED,
              SystemState::INITIALIZING, "init", rec);
    makeEvent(SystemEvent::Type::STATE_CHANGE, SystemState::INITIALIZING,
              SystemState::INITIALIZED, "ready", rec);

    ASSERT_EQ(rec.size(), 2u);

    std::ostringstream name;
    name << axonvex_fs::temp_directory_path().string() << "/axonvex_trace_"
         << std::chrono::steady_clock::now().time_since_epoch().count() << ".json";
    const std::string path = name.str();
    ASSERT_TRUE(rec.saveToFile(axonvex::core::Path(path)));

    TraceReplayer replay;
    ASSERT_TRUE(replay.loadFromFile(axonvex::core::Path(path)));
    EXPECT_TRUE(TraceReplayer::sequencesMatch(rec, replay));

    std::remove(path.c_str());
}

TEST(TraceReplayTest, ReplayInvokesCallbackInOrder) {
    TraceRecorder rec;
    makeEvent(SystemEvent::Type::HEALTH_CHECK, SystemState::RUNNING, SystemState::RUNNING, "h1",
              rec);
    makeEvent(SystemEvent::Type::HEALTH_CHECK, SystemState::RUNNING, SystemState::RUNNING, "h2",
              rec);

    TraceReplayer replay;
    std::ostringstream name2;
    name2 << axonvex_fs::temp_directory_path().string() << "/axonvex_trace2_"
          << std::chrono::steady_clock::now().time_since_epoch().count() << ".json";
    const std::string path = name2.str();
    ASSERT_TRUE(rec.saveToFile(axonvex::core::Path(path)));
    ASSERT_TRUE(replay.loadFromFile(axonvex::core::Path(path)));

    std::vector<std::string> seen;
    replay.replay([&seen](const auto& e) { seen.push_back(e.description); });
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], "h1");
    EXPECT_EQ(seen[1], "h2");

    std::remove(path.c_str());
}

TEST(TraceReplayTest, ClearRecorder) {
    TraceRecorder rec;
    makeEvent(SystemEvent::Type::CONFIGURATION_CHANGED, SystemState::INITIALIZED,
              SystemState::INITIALIZED, "cfg", rec);
    EXPECT_EQ(rec.size(), 1u);
    rec.clear();
    EXPECT_EQ(rec.size(), 0u);
}
