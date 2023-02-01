#ifndef GLIB_SENDERS_AT_COROUTINE_EXIT_HPP
#define GLIB_SENDERS_AT_COROUTINE_EXIT_HPP

// The original idea is taken from libunifex and adapted to glib-senders.

/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <coroutine>
#include <exception>

#include <stdexec/execution.hpp>
#include <exec/task.hpp>

namespace gsenders {

namespace at_coroutine_exit_ {

struct promise_base;
struct final_awaitable {
  auto await_ready() const noexcept -> bool { return false; }

  template <class Promise>
  requires std::derived_from<Promise, promise_base>
  auto await_suspend(std::coroutine_handle<Promise> h) const noexcept
      -> std::coroutine_handle<> {
    auto continuation = h.promise().continuation_;
    h.destroy();
    return continuation;
  }

  void await_resume() const noexcept {}
};

struct promise_base {
  auto initial_suspend() noexcept -> std::suspend_always { return {}; }
  auto final_suspend() noexcept -> final_awaitable { return {}; }
  void return_void() noexcept {}

  [[noreturn]] void unhandled_exception() noexcept { std::terminate(); }

  std::coroutine_handle<> continuation_{};
  bool unhandled_stop_{false};
};

template <class... Ts> struct at_exit_task;

namespace die_on_stop_ {
template <class Receiver> struct receiver {
  Receiver receiver_;

  template <class... Args>
  friend void tag_invoke(stdexec::set_value_t, receiver&& r,
                         Args&&... args) noexcept {
    try {
      stdexec::set_value((Receiver &&) r.receiver_, (Args &&) args...);
    } catch (...) {
      stdexec::set_error((Receiver &&) r.receiver_, std::current_exception());
    }
  }

  template <class E>
  friend void tag_invoke(stdexec::set_error_t, receiver&& r,
                         E&& error) noexcept {
    stdexec::set_error((Receiver &&) r.receiver_, (E &&) error);
  }

  friend void tag_invoke(stdexec::set_stopped_t, receiver&& r) noexcept {
    std::terminate();
  }

  friend auto tag_invoke(stdexec::get_env_t, receiver&& r) noexcept {
    return stdexec::get_env((Receiver &&) r.receiver_);
  }
};

template <class Sender> struct sender {
  Sender sender_;

  template <stdexec::receiver Receiver>
  friend auto tag_invoke(stdexec::connect_t, sender&& s,
                         Receiver&& r) noexcept {
    return stdexec::connect((Sender &&) s.sender_,
                            receiver<Receiver>{(Receiver &&) r});
  }

  friend auto tag_invoke(stdexec::get_env_t, sender&& s) noexcept {
    return stdexec::get_env((Sender &&) s.sender_);
  }
};

struct die_on_stop_t {
  // template <stdexec::sender Sender>
  // auto operator()(Sender&& s) const noexcept -> sender<Sender> {
  //   return sender<Sender>((Sender &&) s);
  // }

  template <class Value> Value&& operator()(Value&& value) const noexcept {
    return (Value &&) value;
  }
};
inline constexpr die_on_stop_t die_on_stop;
} // namespace die_on_stop_
using die_on_stop_::die_on_stop;

template <class... Ts> struct promise : promise_base {
  template <typename Action>
  explicit promise(Action&&, Ts&... ts) noexcept
    : args_(ts...) {}

  auto get_return_object() noexcept -> at_exit_task<Ts...> {
    return at_exit_task<Ts...>{
        std::coroutine_handle<promise>::from_promise(*this)};
  }

  auto unhandled_stop() noexcept -> std::coroutine_handle<promise> {
    unhandled_stop_ = true;
    return std::coroutine_handle<promise>::from_promise(*this);
  }

  template <class Awaitable>
  decltype(auto) await_transform(Awaitable&& awaitable) noexcept {
    return die_on_stop((Awaitable &&) awaitable);
  }

  std::tuple<Ts&...> args_{};
};

template <class... Ts> struct [[nodiscard]] at_exit_task {
  using promise_type = promise<Ts...>;

  explicit at_exit_task(std::coroutine_handle<promise<Ts...>> coro) noexcept
      : continuation_(coro) {}

  at_exit_task(at_exit_task&& that) noexcept
      : continuation_(std::exchange(that.continuation_, {})) {}

  bool await_ready() const noexcept { return false; }

  template <class Promise>
  bool await_suspend(std::coroutine_handle<Promise> parent) noexcept {
    auto tmp = continuation_;
    continuation_.promise().continuation_ = parent.promise().continuation();
    parent.promise().set_continuation(tmp);
    return false;
  }

  auto await_resume() noexcept -> std::tuple<Ts&...> {
    return std::exchange(continuation_, {}).promise().args_;
  }

private:
  std::coroutine_handle<promise<Ts...>> continuation_;
};

struct at_coroutine_exit_t {
private:
  template <typename Action, typename... Ts>
  static at_exit_task<Ts...> at_coroutine_exit(Action action, Ts... ts) {
    co_await ((Action &&) action)((Ts &&) ts...);
  }

public:
  template <typename Action, typename... Ts>
  requires stdexec::__callable<std::decay_t<Action>, std::decay_t<Ts>...>
  at_exit_task<Ts...> operator()(Action&& action, Ts&&... ts) const {
    return at_coroutine_exit_t::at_coroutine_exit((Action &&) action,
                                                  (Ts &&) ts...);
  }
};
inline constexpr at_coroutine_exit_t at_coroutine_exit;
} // namespace at_coroutine_exit_
using at_coroutine_exit_::at_coroutine_exit;
} // namespace gsenders

#endif