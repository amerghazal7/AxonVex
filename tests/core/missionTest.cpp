#include <atomic>
#include <axonvex_core/missionElement.hpp>
#include <axonvex_core/missionPipeline.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace axonvex::core;

// =========================================================================
// Test helpers — configurable mission elements
// =========================================================================

class ImmediateElement : public MissionElement {
  public:
    explicit ImmediateElement(const std::string& name,
                              TransitionResult result = TransitionResult::Default)
        : MissionElement(name), result_(result) {}

    TransitionResult execute() override { execCount_++; return result_; }
    void onEnter() override { enterCount_++; }
    void onExit() override { exitCount_++; }
    void reset() override { execCount_ = enterCount_ = exitCount_ = 0; }

    void setResult(TransitionResult r) { result_ = r; }
    int execCount() const { return execCount_; }
    int enterCount() const { return enterCount_; }
    int exitCount() const { return exitCount_; }

  private:
    TransitionResult result_;
    int execCount_{0};
    int enterCount_{0};
    int exitCount_{0};
};

class DelayedElement : public MissionElement {
  public:
    DelayedElement(const std::string& name, int ticksToWait)
        : MissionElement(name), ticksToWait_(ticksToWait) {}

    TransitionResult execute() override {
        ticks_++;
        if (ticks_ >= ticksToWait_) return TransitionResult::Default;
        return TransitionResult::Awaiting;
    }

    void onEnter() override { entered_ = true; }
    void onExit() override { exited_ = true; }
    void reset() override { ticks_ = 0; entered_ = exited_ = false; }

    int ticks() const { return ticks_; }
    bool entered() const { return entered_; }
    bool exited() const { return exited_; }

  private:
    int ticksToWait_;
    int ticks_{0};
    bool entered_{false};
    bool exited_{false};
};

// =========================================================================
// Sequential pipeline: A -> B -> C -> Finished
// =========================================================================

TEST(MissionPipelineTest, SequentialPipeline) {
    ImmediateElement a("A"), b("B"), c("C");

    MissionPipeline pipeline("SeqPipeline");
    pipeline.addElement(&a);
    pipeline.addElement(&b);
    pipeline.addElement(&c);
    pipeline.addSequentialTransition("A", "B");
    pipeline.addSequentialTransition("B", "C");
    pipeline.setStartElement("A");
    pipeline.initialize();

    EXPECT_EQ(pipeline.status(), PipelineStatus::Idle);

    pipeline.startPipeline();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);
    EXPECT_EQ(pipeline.currentElementName(), "A");

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "B");
    EXPECT_EQ(a.execCount(), 1);
    EXPECT_EQ(a.exitCount(), 1);
    EXPECT_EQ(b.enterCount(), 1);

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "C");

    pipeline.processSync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Finished);
    EXPECT_EQ(c.exitCount(), 1);
}

// =========================================================================
// Conditional pipeline: A -> Default:B, Option1:C
// =========================================================================

TEST(MissionPipelineTest, ConditionalTransitionDefault) {
    ImmediateElement a("A", TransitionResult::Default);
    ImmediateElement b("B"), c("C");

    MissionPipeline pipeline("CondDefault");
    pipeline.addElement(&a);
    pipeline.addElement(&b);
    pipeline.addElement(&c);
    pipeline.addSequentialTransition("A", "B");
    pipeline.addConditionalTransition("A", "C", TransitionResult::Option1);
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "B");
}

TEST(MissionPipelineTest, ConditionalTransitionOption1) {
    ImmediateElement a("A", TransitionResult::Option1);
    ImmediateElement b("B"), c("C");

    MissionPipeline pipeline("CondOpt1");
    pipeline.addElement(&a);
    pipeline.addElement(&b);
    pipeline.addElement(&c);
    pipeline.addSequentialTransition("A", "B");
    pipeline.addConditionalTransition("A", "C", TransitionResult::Option1);
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "C");
}

// =========================================================================
// Awaiting: element returns Awaiting multiple ticks, then Default
// =========================================================================

TEST(MissionPipelineTest, AwaitingThenAdvance) {
    DelayedElement wait("Wait", 3);
    ImmediateElement done("Done");

    MissionPipeline pipeline("AwaitPipeline");
    pipeline.addElement(&wait);
    pipeline.addElement(&done);
    pipeline.addSequentialTransition("Wait", "Done");
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "Wait");
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "Wait");

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "Done");
    EXPECT_TRUE(wait.exited());
    EXPECT_EQ(done.enterCount(), 1);
}

// =========================================================================
// Abort mid-execution
// =========================================================================

TEST(MissionPipelineTest, AbortMidExecution) {
    DelayedElement wait("Wait", 100);

    MissionPipeline pipeline("AbortPipeline");
    pipeline.addElement(&wait);
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);

    pipeline.abort();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Aborted);
}

// =========================================================================
// Restart resets all elements and starts from beginning
// =========================================================================

TEST(MissionPipelineTest, RestartResetsAndRestartsFromBeginning) {
    ImmediateElement a("A"), b("B");

    MissionPipeline pipeline("RestartPipeline");
    pipeline.addElement(&a);
    pipeline.addElement(&b);
    pipeline.addSequentialTransition("A", "B");
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "B");
    EXPECT_EQ(a.execCount(), 1);

    pipeline.restart();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);
    EXPECT_EQ(pipeline.currentElementName(), "A");
    EXPECT_EQ(a.execCount(), 0);

    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "B");
    EXPECT_EQ(a.execCount(), 1);
}

// =========================================================================
// Pause / Resume
// =========================================================================

TEST(MissionPipelineTest, PauseAndResume) {
    DelayedElement wait("Wait", 5);
    ImmediateElement done("Done");

    MissionPipeline pipeline("PausePipeline");
    pipeline.addElement(&wait);
    pipeline.addElement(&done);
    pipeline.addSequentialTransition("Wait", "Done");
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);
    int ticksBefore = wait.ticks();

    pipeline.pause();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Paused);

    pipeline.processSync();
    pipeline.processSync();
    EXPECT_EQ(wait.ticks(), ticksBefore);

    pipeline.resume();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);

    pipeline.processSync();
    EXPECT_EQ(wait.ticks(), ticksBefore + 1);
}

// =========================================================================
// Failed element
// =========================================================================

TEST(MissionPipelineTest, FailedElementTransitionsPipelineToFailed) {
    ImmediateElement fail("Fail", TransitionResult::Failed);

    MissionPipeline pipeline("FailPipeline");
    pipeline.addElement(&fail);
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Failed);
    EXPECT_EQ(fail.exitCount(), 1);
}

// =========================================================================
// onEnter / onExit lifecycle hooks
// =========================================================================

TEST(MissionPipelineTest, LifecycleHooksCalledCorrectly) {
    ImmediateElement a("A"), b("B");

    MissionPipeline pipeline("HooksPipeline");
    pipeline.addElement(&a);
    pipeline.addElement(&b);
    pipeline.addSequentialTransition("A", "B");
    pipeline.initialize();

    EXPECT_EQ(a.enterCount(), 0);
    pipeline.startPipeline();
    EXPECT_EQ(a.enterCount(), 1);

    pipeline.processSync();
    EXPECT_EQ(a.exitCount(), 1);
    EXPECT_EQ(b.enterCount(), 1);

    pipeline.processSync();
    EXPECT_EQ(b.exitCount(), 1);
}

// =========================================================================
// Control port commands
// =========================================================================

TEST(MissionPipelineTest, ControlPortAbort) {
    DelayedElement wait("Wait", 100);

    MissionPipeline pipeline("CtrlAbort");
    pipeline.addElement(&wait);
    pipeline.initialize();
    pipeline.startPipeline();
    pipeline.processSync();

    pipeline.getControlPort()->update(PipelineControl::ABORT);
    pipeline.processAsync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Aborted);
}

TEST(MissionPipelineTest, ControlPortRestart) {
    ImmediateElement a("A"), b("B");

    MissionPipeline pipeline("CtrlRestart");
    pipeline.addElement(&a);
    pipeline.addElement(&b);
    pipeline.addSequentialTransition("A", "B");
    pipeline.initialize();
    pipeline.startPipeline();
    pipeline.processSync();
    EXPECT_EQ(pipeline.currentElementName(), "B");

    pipeline.getControlPort()->update(PipelineControl::RESTART);
    pipeline.processAsync();
    EXPECT_EQ(pipeline.currentElementName(), "A");
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);
}

TEST(MissionPipelineTest, ControlPortPauseResume) {
    DelayedElement wait("Wait", 100);

    MissionPipeline pipeline("CtrlPause");
    pipeline.addElement(&wait);
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.getControlPort()->update(PipelineControl::PAUSE);
    pipeline.processAsync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Paused);

    pipeline.getControlPort()->update(PipelineControl::RESUME);
    pipeline.processAsync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Executing);
}

// =========================================================================
// Status port reflects current state
// =========================================================================

TEST(MissionPipelineTest, StatusPortWritesCurrentStatus) {
    ImmediateElement a("A");

    MissionPipeline pipeline("StatusPort");
    pipeline.addElement(&a);
    pipeline.initialize();
    pipeline.startPipeline();

    pipeline.processSync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Finished);
}

// =========================================================================
// First added element is default start
// =========================================================================

TEST(MissionPipelineTest, FirstAddedElementIsDefaultStart) {
    ImmediateElement first("First"), second("Second");

    MissionPipeline pipeline("DefaultStart");
    pipeline.addElement(&first);
    pipeline.addElement(&second);
    pipeline.addSequentialTransition("First", "Second");
    pipeline.initialize();
    pipeline.startPipeline();

    EXPECT_EQ(pipeline.currentElementName(), "First");
}

// =========================================================================
// Type description
// =========================================================================

TEST(MissionPipelineTest, TypeDescription) {
    MissionPipeline pipeline("Desc");
    EXPECT_EQ(pipeline.getTypeDescription(), "MissionPipeline");
}

// =========================================================================
// C22: status_/currentElement_ were mutated from the scheduler thread
// (processSync) and the user control API with no synchronization.
// =========================================================================

// TSan is the real assertion here: the loop just gives it interleavings to
// catch. A cyclic graph keeps processSync() ticking (never Finished) for
// the whole deadline window so thread A stays busy racing thread B.
TEST(MissionPipelineTest, ConcurrentControlAndTickAreRaceFree) {
    ImmediateElement a("A"), b("B"), c("C");

    MissionPipeline pipeline("ConcurrentPipeline");
    pipeline.addElement(&a);
    pipeline.addElement(&b);
    pipeline.addElement(&c);
    pipeline.addSequentialTransition("A", "B");
    pipeline.addSequentialTransition("B", "C");
    pipeline.addSequentialTransition("C", "A");
    pipeline.initialize();
    pipeline.startPipeline();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    std::thread ticker([&pipeline, deadline]() {
        while (std::chrono::steady_clock::now() < deadline) {
            pipeline.processSync();
        }
    });

    std::thread controller([&pipeline, deadline]() {
        while (std::chrono::steady_clock::now() < deadline) {
            pipeline.pause();
            pipeline.resume();
            (void)pipeline.status();
            (void)pipeline.currentElementName();
        }
    });

    ticker.join();
    controller.join();
    pipeline.abort();
    SUCCEED();
}

// C22: transitions referencing unknown elements were only discovered as a
// wedged pipeline at runtime (silent early return, status stuck Idle).
// startPipeline() now validates the graph first and surfaces the dangling
// reference through ProcessingUnit's setError()/getLastError().
TEST(MissionPipelineTest, StartPipelineRejectsDanglingTransition) {
    ImmediateElement a("A");

    MissionPipeline pipeline("DanglingTransition");
    pipeline.addElement(&a);
    pipeline.addSequentialTransition("A", "Typo");
    pipeline.initialize();

    pipeline.startPipeline();

    EXPECT_EQ(pipeline.status(), PipelineStatus::Failed);
    EXPECT_TRUE(pipeline.hasError());
    EXPECT_NE(pipeline.getLastError().find("Typo"), std::string::npos);
}

class PauseOnEnterElement : public MissionElement {
  public:
    PauseOnEnterElement(const std::string& name, MissionPipeline& pipeline)
        : MissionElement(name), pipeline_(pipeline) {}

    TransitionResult execute() override {
        return TransitionResult::Default;
    }
    void onEnter() override {
        pipeline_.pause();
    }

  private:
    MissionPipeline& pipeline_;
};

// C22: a hook that re-enters the control API must not deadlock — stateMutex_
// is never held across a MissionElement hook call. Deadline-guarded: a
// regression hangs rather than fails outright.
TEST(MissionPipelineTest, HookReenteringControlApiDoesNotDeadlock) {
    MissionPipeline pipeline("ReentrantPipeline");
    PauseOnEnterElement a("A", pipeline);
    pipeline.addElement(&a);
    pipeline.initialize();

    auto done = std::make_shared<std::atomic<bool>>(false);

    std::thread worker([&pipeline, done]() {
        pipeline.startPipeline();
        done->store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!done->load()) {
        worker.detach(); // wedged; leak it rather than hang the suite
        FAIL() << "onEnter() re-entering pause() deadlocked startPipeline()";
        return;
    }
    worker.join();

    EXPECT_EQ(pipeline.status(), PipelineStatus::Paused);
}

} // namespace
