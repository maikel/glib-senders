#ifndef GLIB_SENDERS_INTRUSIVE_QUEUE_HPP
#define GLIB_SENDERS_INTRUSIVE_QUEUE_HPP

namespace gsenders {

/// @brief A queue of objects that are linked together.
///
/// The objects must have a pointer to the next object in the queue.
/// The queue does not own the objects and is not thread-safe.
///
/// @tparam T The type of the objects.
/// @tparam NextPtr A pointer to the next pointer in the object.
template <typename T, T* T::*NextPtr> class intrusive_queue {
public:
  using node_pointer = T*;

  [[nodiscard]] auto empty() const noexcept -> bool;

  auto push_back(node_pointer t) noexcept -> void;

  auto pop_front() noexcept -> node_pointer;

  auto pop_all() noexcept -> node_pointer;

private:
  node_pointer head_{nullptr};
  node_pointer tail_{nullptr};
};

///////////////////////////////////////////////////////////////////////////////
// Implementation

template <typename T, T* T::*NextPtr>
auto intrusive_queue<T, NextPtr>::empty() const noexcept -> bool {
  return head_ == nullptr;
}

template <typename T, T* T::*NextPtr>
auto intrusive_queue<T, NextPtr>::push_back(node_pointer t) noexcept -> void {
  t->*NextPtr = nullptr;
  if (tail_ == nullptr)
    head_ = tail_ = t;
  else {
    tail_->*NextPtr = t;
    tail_ = t;
  }
}

template <typename T, T* T::*NextPtr>
auto intrusive_queue<T, NextPtr>::pop_front() noexcept -> node_pointer {
  node_pointer t = head_;
  if (t != nullptr) {
    head_ = t->*NextPtr;
    if (head_ == nullptr) {
      tail_ = nullptr;
    }
  }
  return t;
}

template <typename T, T* T::*NextPtr>
auto intrusive_queue<T, NextPtr>::pop_all() noexcept -> node_pointer {
  node_pointer t = head_;
  head_ = tail_ = nullptr;
  return t;
}

} // namespace gsenders

#endif