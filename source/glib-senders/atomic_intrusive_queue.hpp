#ifndef GLIB_SENDERS_ATOMIC_INTRUSIVE_QUEUE_HPP
#define GLIB_SENDERS_ATOMIC_INTRUSIVE_QUEUE_HPP

#include <atomic>
#include <memory>

namespace gsenders {

template <typename T, std::atomic<T*> T::*NextPtr>
class atomic_intrusive_queue {
public:
  using node_pointer = T*;
  using atomic_node_pointer = std::atomic<T*>;

  [[nodiscard]] auto empty() const noexcept -> bool;

  auto push_back(node_pointer t) noexcept -> void;

  auto pop_front() noexcept -> node_pointer;

  auto pop_all() noexcept -> node_pointer;

private:
  atomic_node_pointer head_{nullptr};
  atomic_node_pointer tail_{nullptr};
};

///////////////////////////////////////////////////////////////////////////////
// Implementation

template <typename T, std::atomic<T*> T::*NextPtr>
auto atomic_intrusive_queue<T, NextPtr>::empty() const noexcept -> bool {
  return head_.load() == nullptr;
}

template <typename T, std::atomic<T*> T::*NextPtr>
auto atomic_intrusive_queue<T, NextPtr>::push_back(node_pointer t) noexcept
    -> void {
  node_pointer old_tail = tail_.load();
  while (!tail_.compare_exchange_weak(old_tail, t))
    ;
  if (old_tail == nullptr) {
    head_.store(t);
  } else {
    old_tail->*NextPtr = t;
  }
}

template <typename T, std::atomic<T*> T::*NextPtr>
auto atomic_intrusive_queue<T, NextPtr>::pop_front() noexcept -> node_pointer {
  node_pointer old_head = head_.load();
  while (old_head != nullptr) {
    node_pointer new_head =
        (old_head->*NextPtr).load();
    if (head_.compare_exchange_weak(old_head, new_head)) {
      if (new_head == nullptr) {
        node_pointer current_tail = old_head;
        if (!tail_.compare_exchange_strong(current_tail, nullptr)) {
          head_.store(current_tail);
        }
      }
      old_head->*NextPtr = nullptr;
      return old_head;
    }
  }
  return nullptr;
}

template <typename T, std::atomic<T*> T::*NextPtr>
auto atomic_intrusive_queue<T, NextPtr>::pop_all() noexcept -> node_pointer {
  node_pointer old_head = head_.load();
  while (old_head != nullptr) {
    if (head_.compare_exchange_weak(old_head, nullptr)) {
      tail_.store(nullptr);
      return old_head;
    }
  }
  return nullptr;
}

} // namespace gsenders

#endif