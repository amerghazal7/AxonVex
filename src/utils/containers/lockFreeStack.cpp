/**
 * @file lockFreeStack.cpp
 * @brief Lock-free stack implementation
 */

#include <axonvex/utils/containers/lockFreeStack.hpp>
#include <string>

namespace axonvex::utils::containers {

template<typename T>
void LockFreeStack<T>::push(T const& data) {
    Node* const new_node = new Node(data);
    Node* old_head = head_.load();
    new_node->next = old_head;
    while (!head_.compare_exchange_weak(old_head, new_node)) {
        new_node->next = old_head;
    }
}

template<typename T>
std::shared_ptr<T> LockFreeStack<T>::pop() {
    Node* old_head = head_.load();
    while (old_head && !head_.compare_exchange_weak(old_head, old_head->next.load()));
    return old_head ? std::shared_ptr<T>(new T(old_head->data)) : std::shared_ptr<T>();
}

// Explicit template instantiations for common types
template class LockFreeStack<int>;
template class LockFreeStack<double>;
template class LockFreeStack<std::string>;

} // namespace axonvex::utils::containers