#pragma once

#include <axonvex_core/processingUnit.hpp>
#include <functional>

namespace axonvex::core {

/**
 * @brief Data source PU — middleware pushes data in, pipeline reads from ports.
 *
 * Sync:  OutputPort<T>         (port 0) — downstream PUs read each cycle
 * Async: AsyncOutputPort<bool> (port 1) — trigger fires on each pushData call
 *
 * External code (adapter callback, hardware driver, timer) calls pushData()
 * to inject data. No protocol dependency — the PU is protocol-agnostic.
 */
template <typename T>
class SubscriberUnit : public ProcessingUnit {
  public:
    enum OP { OUTPUT = 0, TRIGGER = 1 };

    explicit SubscriberUnit(const std::string& name) : ProcessingUnit(name) {
        output_ = createOutputPort<T>(OUTPUT, "output");
        output_->setThreadSafe(true);
        trigger_ = createAsyncOutputPort<bool>(TRIGGER, "trigger");
    }

    void pushData(const T& data) {
        output_->write(data);
        trigger_->write(true);
    }

    OutputPort<T>* getOutput() { return output_; }
    AsyncOutputPort<bool>* getTrigger() { return trigger_; }

    void processSync() override {}
    void processAsync() override {}
    void reset() override { setState(ExecutionState::INITIALIZED); }
    void initialize() override { setState(ExecutionState::INITIALIZED); }
    std::string getTypeDescription() override { return "SubscriberUnit"; }

  private:
    OutputPort<T>* output_;
    AsyncOutputPort<bool>* trigger_;
};

/**
 * @brief Data sink PU — pipeline writes to ports, adapter publishes out.
 *
 * Sync:  InputPort<T>       (port 0) — upstream PUs connect here
 * Async: AsyncInputPort<T>  (port 1) — for event-driven publish
 *
 * The app sets a publish callback via setPublishCallback(). processSync()
 * reads the sync port and calls it; processAsync() reads the async port.
 */
template <typename T>
class PublisherUnit : public ProcessingUnit {
  public:
    enum IP { INPUT = 0, INPUT_ASYNC = 1 };

    using PublishFn = std::function<void(const T&)>;

    explicit PublisherUnit(const std::string& name) : ProcessingUnit(name) {
        input_ = createInputPort<T>(INPUT, "input");
        asyncInput_ = createAsyncInputPort<T>(INPUT_ASYNC, "input_async");
    }

    void setPublishCallback(PublishFn cb) { publishCb_ = std::move(cb); }

    InputPort<T>* getInput() { return input_; }
    AsyncInputPort<T>* getAsyncInput() { return asyncInput_; }

    void processSync() override {
        if (!input_->hasNewData()) return;
        auto data = input_->read();
        input_->clearNewDataFlag();
        if (publishCb_) publishCb_(data);
    }

    void processAsync() override {
        if (!asyncInput_->wasUpdated()) return;
        auto data = asyncInput_->read();
        if (publishCb_) publishCb_(data);
    }

    void reset() override { setState(ExecutionState::INITIALIZED); }
    void initialize() override { setState(ExecutionState::INITIALIZED); }
    std::string getTypeDescription() override { return "PublisherUnit"; }

  private:
    InputPort<T>* input_;
    AsyncInputPort<T>* asyncInput_;
    PublishFn publishCb_;
};

/**
 * @brief Service receiver PU — middleware pushes requests, pipeline handles.
 *
 * Async: AsyncOutputPort<T> (port 0) — fires per incoming request
 * Sync:  OutputPort<T>      (port 1) — last request value for sync readers
 *
 * External code calls pushRequest() when a service request arrives.
 */
template <typename T>
class ServerUnit : public ProcessingUnit {
  public:
    enum OP { OUTPUT_ASYNC = 0, OUTPUT = 1 };

    explicit ServerUnit(const std::string& name) : ProcessingUnit(name) {
        asyncOutput_ = createAsyncOutputPort<T>(OUTPUT_ASYNC, "output_async");
        output_ = createOutputPort<T>(OUTPUT, "output");
        output_->setThreadSafe(true);
    }

    void pushRequest(const T& data) {
        asyncOutput_->write(data);
        output_->write(data);
    }

    AsyncOutputPort<T>* getAsyncOutput() { return asyncOutput_; }
    OutputPort<T>* getOutput() { return output_; }

    void processSync() override {}
    void processAsync() override {}
    void reset() override { setState(ExecutionState::INITIALIZED); }
    void initialize() override { setState(ExecutionState::INITIALIZED); }
    std::string getTypeDescription() override { return "ServerUnit"; }

  private:
    AsyncOutputPort<T>* asyncOutput_;
    OutputPort<T>* output_;
};

/**
 * @brief Service caller PU — pipeline sends request, gets completion signal.
 *
 * Async: AsyncInputPort<T>    (port 0) — upstream PU writes the request
 * Async: AsyncOutputPort<bool> (port 1) — fires true on completion
 *
 * The app sets a request callback via setRequestCallback(). processAsync()
 * reads the input and calls it.
 */
template <typename T>
class ClientUnit : public ProcessingUnit {
  public:
    enum IP { INPUT_ASYNC = 0 };
    enum OP { FINISHED_ASYNC = 1 };

    using RequestFn = std::function<void(const T&)>;

    explicit ClientUnit(const std::string& name) : ProcessingUnit(name) {
        asyncInput_ = createAsyncInputPort<T>(INPUT_ASYNC, "input_async");
        finishedOutput_ = createAsyncOutputPort<bool>(FINISHED_ASYNC, "finished");
    }

    void setRequestCallback(RequestFn cb) { requestCb_ = std::move(cb); }

    AsyncInputPort<T>* getAsyncInput() { return asyncInput_; }
    AsyncOutputPort<bool>* getFinishedOutput() { return finishedOutput_; }

    void processSync() override {}

    void processAsync() override {
        if (!asyncInput_->wasUpdated()) return;
        auto data = asyncInput_->read();
        if (requestCb_) {
            requestCb_(data);
            finishedOutput_->write(true);
        }
    }

    void reset() override { setState(ExecutionState::INITIALIZED); }
    void initialize() override { setState(ExecutionState::INITIALIZED); }
    std::string getTypeDescription() override { return "ClientUnit"; }

  private:
    AsyncInputPort<T>* asyncInput_;
    AsyncOutputPort<bool>* finishedOutput_;
    RequestFn requestCb_;
};

} // namespace axonvex::core
