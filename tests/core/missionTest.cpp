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

    // Finding 1 (whole-branch review): abort() now only enqueues — it takes
    // effect at the next drain (processAsync()), on the scheduler thread,
    // not synchronously on this call.
    pipeline.abort();
    pipeline.processAsync();
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

    // Finding 1: restart() now only enqueues — drained on the next
    // processAsync(), which is where the reset()/onEnter() hooks it
    // triggers actually run.
    pipeline.restart();
    pipeline.processAsync();
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

    // Finding 1: pause()/resume() now only enqueue — drained on the next
    // processAsync().
    pipeline.pause();
    pipeline.processAsync();
    EXPECT_EQ(pipeline.status(), PipelineStatus::Paused);

    pipeline.processSync();
    pipeline.processSync();
    EXPECT_EQ(wait.ticks(), ticksBefore);

    pipeline.resume();
    pipeline.processAsync();
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
    // Finding 1: startPipeline()'s onEnter() dispatch is deferred — drained
    // on the next processAsync(), on the scheduler thread.
    pipeline.processAsync();
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
//
// Finding 1 (whole-branch review) went further: even with stateMutex_
// guarding the members, abort()/restart()/reset() were still dispatching
// MissionElement hooks (onExit/reset/onEnter) from the CALLING thread,
// which could race an in-flight, unlocked execute() on the scheduler
// thread — same object, unsynchronized. Fix: the direct control API only
// enqueues; every hook now runs from processAsync()'s drain, on whichever
// thread calls it (the "scheduler thread" in production).
// =========================================================================

// TSan is the real assertion here: the loop just gives it interleavings to
// catch. A cyclic graph keeps ticking (never Finished) for the whole
// deadline window so thread A stays busy racing thread B. The ticker now
// drains (processAsync()) before it ticks (processSync()) each iteration,
// mirroring executeTask()'s real order, so the controller thread's
// pause()/resume() commands actually get applied during the stress run
// instead of sitting unread in controlPort_ for the whole test.
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
            pipeline.processAsync(); // drain: the only thread that ever
                                     // calls a MissionElement hook.
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
    pipeline.processAsync();
    SUCCEED();
}

// Finding 1's reviewer-provided TSan repro: a MissionElement whose
// execute() and onExit() write the same plain (non-atomic) field. Pre-fix,
// abort()/restart() dispatched onExit() directly on the calling thread
// while a concurrent ticker thread's unlocked execute() call could still
// be in flight on the SAME object — a genuine data race, reliably caught
// by TSan (see the fix commit message for the captured pre-fix report).
// Post-fix, abort()/restart() only enqueue; the drain (processAsync())
// only ever runs from the ticker thread here, so onExit() and execute()
// are always sequenced relative to each other, never concurrent.
class RacyWriteElement : public MissionElement {
  public:
    explicit RacyWriteElement(const std::string& name) : MissionElement(name) {}

    TransitionResult execute() override {
        field_ = 1;                        // plain write — the race target
        return TransitionResult::Awaiting; // keep ticking for the whole window
    }
    void onExit() override {
        field_ = 2; // plain write — races execute() pre-fix
    }
    void reset() override {
        field_ = 0;
    }

  private:
    int field_{0};
};

TEST(MissionPipelineTest, DirectAbortDoesNotRaceConcurrentTick) {
    RacyWriteElement racy("Racy");

    MissionPipeline pipeline("TsanRegressionPipeline");
    pipeline.addElement(&racy);
    pipeline.initialize();
    pipeline.startPipeline();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    std::thread ticker([&pipeline, deadline]() {
        while (std::chrono::steady_clock::now() < deadline) {
            pipeline.processAsync();
            pipeline.processSync();
        }
    });

    // abort() forces Aborted; restart() puts it back to Executing so the
    // ticker keeps calling execute() for the whole deadline window instead
    // of idling after the first abort().
    std::thread controller([&pipeline, deadline]() {
        while (std::chrono::steady_clock::now() < deadline) {
            pipeline.abort();
            pipeline.restart();
        }
    });

    ticker.join();
    controller.join();
    SUCCEED(); // TSan is the real assertion.
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

// C22 / Finding 1: a hook that re-enters the control API must not
// deadlock. Post-Finding-1, startPipeline()'s onEnter() dispatch and the
// pause() it re-enters are BOTH deferred — neither runs synchronously
// inside a hook call, and neither is dispatched while stateMutex_ is held.
// startPipeline() itself (on the worker thread below) now does no hook
// dispatch at all, so it returns immediately; the onEnter()->pause() chain
// only runs once something drains it (processAsync()), which stands in
// for the scheduler thread here. Deadline-guarded throughout: a regression
// hangs or never reaches Paused rather than failing outright.
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

    const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done->load() && std::chrono::steady_clock::now() < startDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!done->load()) {
        worker.detach(); // wedged; leak it rather than hang the suite
        FAIL() << "startPipeline() hung";
        return;
    }
    worker.join();

    // Drain from a third thread (standing in for the scheduler): each
    // processAsync() call may itself dispatch onEnter(), which calls
    // pause() (re-enters the control API), enqueuing another command that
    // a later iteration of this same loop drains. Bounded, not
    // synchronous-in-one-call, by design — see the class comment above
    // "Execution control" in missionPipeline.hpp.
    bool reachedPaused = false;
    const auto tickDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < tickDeadline) {
        pipeline.processAsync();
        if (pipeline.status() == PipelineStatus::Paused) {
            reachedPaused = true;
            break;
        }
    }

    EXPECT_TRUE(reachedPaused)
        << "onEnter() re-entering pause() never materialized (deadlock or lost command)";
    EXPECT_EQ(pipeline.status(), PipelineStatus::Paused);
}

} // namespace
