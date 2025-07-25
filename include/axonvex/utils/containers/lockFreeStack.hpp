/**
 * @file lockFreeStack.hpp
 * @brief Lock-free stack implementation
 */

#pragma once

#include <atomic>
#include <memory>

namespace axonvex::utils::containers {

template<typename T>
class LockFreeStack {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;
        Node(T const& data_) : data(data_), next(nullptr) {}
    };
    
    std::atomic<Node*> head_;

public:
    LockFreeStack() : head_(nullptr) {}
    
    ~LockFreeStack() {
        while (Node* const old_head = head_.load()) {
            head_ = old_head->next.load();
            delete old_head;
        }
    }
    
    void push(T const& data);
    std::shared_ptr<T> pop();
    bool empty() const { return head_.load() == nullptr; }
};

} // namespace axonvex::utils::containers