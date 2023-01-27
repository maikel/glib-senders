#ifndef GLIB_SENDERS_GLIB_IO_CONTEXT_HPP
#define GLIB_SENDERS_GLIB_IO_CONTEXT_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>

#include <stdexec/execution.hpp>
#include <stdexec/stop_token.hpp>

#include "glib.h"

namespace gsenders {
using stdexec::nothrow_tag_invocable;
using stdexec::tag_invocable;
using stdexec::tag_invoke_result;
using stdexec::tag_invoke_result_t;

struct wait_for_t {
  template <class S, typename Duration>
  requires stdexec::scheduler<S> && tag_invocable<wait_for_t, S, Duration> &&
           stdexec::sender<tag_invoke_result_t<wait_for_t, S, Duration>>
  [[nodiscard]] auto operator()(S&& scheduler, Duration duration) const
      noexcept(nothrow_tag_invocable<wait_for_t, S, Duration>) {
    return tag_invoke(wait_for_t{}, std::forward<S>(scheduler), duration);
  }
};
inline constexpr wait_for_t wait_for{};

struct wait_until_t {
  template <class S, typename... Args>
  requires stdexec::scheduler<S> && tag_invocable<wait_until_t, S, Args...> &&
           stdexec::sender<tag_invoke_result_t<wait_until_t, S, Args...>>
  [[nodiscard]] auto operator()(S&& scheduler, Args&&... args) const
      noexcept(stdexec::nothrow_tag_invocable<wait_until_t, S, Args...>) {
    return tag_invoke(wait_until_t{}, std::forward<S>(scheduler),
                      std::forward<Args>(args)...);
  }
};
inline constexpr wait_until_t wait_until{};

class glib_io_context;

struct schedule_sender;
struct wait_for_sender;
struct wait_until_sender;

enum class io_condition { is_readable = 1, is_writeable = 2, is_error = 4 };
auto operator|(io_condition, io_condition) noexcept -> io_condition;
auto operator&(io_condition, io_condition) noexcept -> bool;

class glib_scheduler {
public:
  explicit glib_scheduler(glib_io_context& ctx) : context_{&ctx} {}

private:
  friend class schedule_sender;
  friend class wait_until_sender;

  auto get_GMainContext() const noexcept -> ::GMainContext*;

  friend auto tag_invoke(stdexec::schedule_t, glib_scheduler self) noexcept
      -> schedule_sender;

  friend auto tag_invoke(wait_for_t, glib_scheduler self,
                         std::chrono::milliseconds dur) noexcept
      -> wait_for_sender;

  friend auto tag_invoke(wait_until_t, glib_scheduler self, int fd,
                         io_condition condition) noexcept -> wait_until_sender;

  friend bool operator==(const glib_scheduler&,
                         const glib_scheduler&) = default;

  glib_io_context* context_;
};

class glib_io_context {
public:
  glib_io_context();
  explicit glib_io_context(::GMainContext* context);

  glib_io_context(const glib_io_context&) = delete;
  glib_io_context& operator=(const glib_io_context&) = delete;

  glib_io_context(glib_io_context&&) = delete;
  glib_io_context& operator=(glib_io_context&&) = delete;

  [[nodiscard]] auto get_scheduler() noexcept -> glib_scheduler;

  auto run() -> void;

  auto stop() -> void;

private:
  friend class glib_scheduler;
  struct context_destroy {
    void operator()(::GMainContext* pointer) const noexcept;
  };
  std::unique_ptr<::GMainContext, context_destroy> context_{nullptr};

  struct loop_destroy {
    void operator()(::GMainLoop* pointer) const noexcept;
  };
  std::unique_ptr<::GMainLoop, loop_destroy> loop_{nullptr};
};

///////////////////////////////////////////////////////////////////////////////
// Implementation

template <typename Receiver> class schedule_operation {
private:
  inline static ::GSourceFuncs vtable_{
      [](::GSource*, int* timeout) -> gboolean {
        if (timeout) {
          *timeout = -1;
        }
        return true;
      },       // prepare
      nullptr, // check
      [](::GSource*, ::GSourceFunc callback, gpointer data) -> gboolean {
        if (callback) {
          return callback(data);
        }
        return G_SOURCE_REMOVE;
      },       // dispatch
      nullptr, // finalize
      nullptr,
      nullptr};

  [[no_unique_address]] Receiver receiver_;
  ::GMainContext* context_;

  struct on_stop_requested {
    stdexec::in_place_stop_source& stop_source_;
    ::GMainContext* context_;
    void operator()() noexcept {
      stop_source_.request_stop();
      ::g_main_context_wakeup(context_);
    }
  };
  stdexec::in_place_stop_source stop_source_{};
  using on_stop = std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<Receiver>&>::template callback_type<on_stop_requested>>;
  on_stop on_stop_{};

  friend auto tag_invoke(stdexec::start_t, schedule_operation& self) noexcept
      -> void {
    self.on_stop_.emplace(
        stdexec::get_stop_token(stdexec::get_env(self.receiver_)),
        on_stop_requested{self.stop_source_, self.context_});
    ::GSource* source = ::g_source_new(&vtable_, sizeof(::GSource));
    ::g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          auto& self = *static_cast<schedule_operation*>(data);
          try {
            self.on_stop_.reset();
            if (self.stop_source_.stop_requested()) {
              stdexec::set_stopped(std::move(self.receiver_));
            } else {
              stdexec::set_value(std::move(self.receiver_));
            }
          } catch (...) {
            stdexec::set_error(std::move(self.receiver_),
                               std::current_exception());
          }
          return G_SOURCE_REMOVE;
        },
        &self, nullptr);
    ::g_source_attach(source, self.context_);
    ::g_source_unref(source);
  }

public:
  schedule_operation(::GMainContext* context, Receiver&& receiver)
      : receiver_{std::move(receiver)}, context_{context} {}
  schedule_operation(schedule_operation&&) = delete;
};

class schedule_sender {
public:
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(),
                                     stdexec::set_error_t(std::exception_ptr)>;

  explicit schedule_sender(glib_scheduler scheduler) : scheduler_(scheduler) {}

  struct attrs {
    glib_scheduler scheduler_;
    friend glib_scheduler
    tag_invoke(stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
               const attrs& self) noexcept {
      return self.scheduler_;
    }
  };

private:
  glib_scheduler scheduler_;

  auto get_GMainContext() const noexcept -> ::GMainContext* {
    return scheduler_.get_GMainContext();
  }

  template <typename R>
  requires stdexec::receiver<R>
  friend auto
  tag_invoke(stdexec::connect_t, const schedule_sender& self,
             R&& receiver) noexcept(std::
                                        is_nothrow_constructible_v<
                                            std::remove_cvref_t<R>, R>)
      -> schedule_operation<std::remove_cvref_t<R>> {
    return {self.get_GMainContext(), std::forward<R>(receiver)};
  }

  friend attrs tag_invoke(stdexec::get_attrs_t,
                          const schedule_sender& self) noexcept {
    return attrs{self.scheduler_};
  }
};

template <typename Receiver>
requires stdexec::receiver<Receiver>
struct wait_for_operation {
  [[no_unique_address]] Receiver receiver_{};
  ::GMainContext* context_{nullptr};
  std::chrono::milliseconds timeout_{};

  struct on_stop_requested {
    stdexec::in_place_stop_source& stop_source_;
    ::GMainContext* context_;
    GSource* source_;
    void operator()() noexcept {
      stop_source_.request_stop();
      ::g_source_set_ready_time(source_, 0);
      ::g_main_context_wakeup(context_);
    }
  };
  stdexec::in_place_stop_source stop_source_{};
  using on_stop = std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<Receiver>&>::template callback_type<on_stop_requested>>;
  on_stop on_stop_{};

  friend auto tag_invoke(stdexec::start_t, wait_for_operation& op) noexcept
      -> void {
    ::GSource* source = ::g_timeout_source_new(op.timeout_.count());
    op.on_stop_.emplace(
        stdexec::get_stop_token(stdexec::get_env(op.receiver_)),
        on_stop_requested{op.stop_source_, op.context_, source});
    ::g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          auto& self = *static_cast<wait_for_operation*>(data);
          try {
            self.on_stop_.reset();
            if (self.stop_source_.stop_requested()) {
              stdexec::set_stopped(std::move(self.receiver_));
            } else {
              stdexec::set_value(std::move(self.receiver_));
            }
          } catch (...) {
            stdexec::set_error(std::move(self.receiver_),
                               std::current_exception());
          }
          return G_SOURCE_REMOVE;
        },
        &op, nullptr);
    ::g_source_attach(source, op.context_);
    ::g_source_unref(source);
  }
};

struct wait_for_sender {
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(),
                                     stdexec::set_error_t(std::exception_ptr),
                                     stdexec::set_stopped_t()>;

  ::GMainContext* context_{nullptr};
  std::chrono::milliseconds timeout_{};

  template <typename Receiver>
  requires stdexec::receiver<Receiver>
  friend auto tag_invoke(stdexec::connect_t, wait_for_sender self,
                         Receiver&& receiver)
      -> wait_for_operation<std::remove_cvref_t<Receiver>> {
    return {std::forward<Receiver>(receiver), self.context_, self.timeout_};
  }
};

template <typename Receiver> struct wait_until_operation {
  ::GMainContext* context_{nullptr};
  int fd_{};
  io_condition condition_{};
  Receiver receiver_{};

  struct wait_until_source : ::GSource {
    stdexec::in_place_stop_source* stop_source_;
  };
  inline static GSourceFuncs vtable_{
      [](::GSource* source, int* timeout) -> gboolean {
        if (timeout) {
          *timeout = -1;
        }
        if (source) {
          auto& self = *static_cast<wait_until_source*>(source);
          return self.stop_source_->stop_requested();
        }
        return true;
      },       // prepare
      nullptr, // check
      [](::GSource*, ::GSourceFunc callback, gpointer data) -> gboolean {
        if (callback) {
          return callback(data);
        }
        return G_SOURCE_REMOVE;
      },       // dispatch
      nullptr, // finalize
      nullptr,
      nullptr};

  struct on_stop_requested {
    stdexec::in_place_stop_source& stop_source_;
    ::GMainContext* context_;
    void operator()() noexcept {
      stop_source_.request_stop();
      ::g_main_context_wakeup(context_);
    }
  };
  stdexec::in_place_stop_source stop_source_{};
  using on_stop = std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<Receiver>&>::template callback_type<on_stop_requested>>;
  on_stop on_stop_{};

  ::GIOCondition get_g_io_condition() {
    ::GIOCondition g_condition{};
    if (condition_ & io_condition::is_readable) {
      g_condition = (GIOCondition)(g_condition | G_IO_IN | G_IO_ERR | G_IO_HUP);
    }
    if (condition_ & io_condition::is_writeable) {
      g_condition =
          (GIOCondition)(g_condition | G_IO_OUT | G_IO_ERR | G_IO_HUP);
    }
    return g_condition;
  }

  friend auto tag_invoke(stdexec::start_t, wait_until_operation& op) noexcept
      -> void {
    auto source = static_cast<wait_until_source*>(
        ::g_source_new(&vtable_, sizeof(wait_until_source)));
    source->stop_source_ = &op.stop_source_;
    op.on_stop_.emplace(stdexec::get_stop_token(stdexec::get_env(op.receiver_)),
                        on_stop_requested{op.stop_source_, op.context_});
    ::g_source_add_unix_fd(source, op.fd_, op.get_g_io_condition());
    ::g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          if (!data) {
            return G_SOURCE_REMOVE;
          }
          auto& self = *static_cast<wait_until_operation*>(data);
          try {
            self.on_stop_.reset();
            if (self.stop_source_.stop_requested()) {
              stdexec::set_stopped(std::move(self.receiver_));
            } else {
              stdexec::set_value(std::move(self.receiver_), self.fd_);
            }
          } catch (...) {
            stdexec::set_error(std::move(self.receiver_),
                               std::current_exception());
          }
          return G_SOURCE_REMOVE;
        },
        &op, nullptr);
    ::g_source_attach(source, op.context_);
    ::g_source_unref(source);
  }
};

struct wait_until_sender {
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(int),
                                     stdexec::set_error_t(std::exception_ptr),
                                     stdexec::set_stopped_t()>;
  glib_scheduler scheduler_;
  int fd_{};
  io_condition condition_;

  auto get_GMainContext() const noexcept -> ::GMainContext* {
    return scheduler_.get_GMainContext();
  }

  template <typename Receiver>
  requires stdexec::receiver<Receiver>
  friend auto tag_invoke(stdexec::connect_t, wait_until_sender self,
                         Receiver&& receiver)
      -> wait_until_operation<std::remove_cvref_t<Receiver>> {
    return {self.get_GMainContext(), self.fd_, self.condition_,
            std::forward<Receiver>(receiver)};
  }

  struct attrs {
    glib_scheduler scheduler_;
    friend glib_scheduler
    tag_invoke(stdexec::get_completion_scheduler_t<stdexec::set_value_t>,
               const attrs& self) noexcept {
      return self.scheduler_;
    }
  };

  friend attrs tag_invoke(stdexec::get_attrs_t,
                          const wait_until_sender& self) noexcept {
    return attrs{self.scheduler_};
  }
};

} // namespace gsenders

#endif
