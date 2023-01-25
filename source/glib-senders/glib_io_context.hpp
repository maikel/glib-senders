#ifndef GLIB_SENDERS_GLIB_IO_CONTEXT_HPP
#define GLIB_SENDERS_GLIB_IO_CONTEXT_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>

#include <exec/inline_scheduler.hpp>
#include <stdexec/execution.hpp>

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
  [[nodiscard]] auto operator()(S&& scheduler, Duration duration) noexcept(
      nothrow_tag_invocable<wait_for_t, S, Duration>) {
    return tag_invoke(wait_for_t{}, std::forward<S>(scheduler), duration);
  }
};
inline constexpr wait_for_t wait_for{};

struct wait_until_t {
  template <class S, typename... Args>
  requires stdexec::scheduler<S> && tag_invocable<wait_until_t, S, Args...> &&
           stdexec::sender<tag_invoke_result_t<wait_until_t, S, Args...>>
  [[nodiscard]] auto operator()(S&& scheduler, Args&&... args) noexcept(
      stdexec::nothrow_tag_invocable<wait_until_t, S, Args...>) {
    return tag_invoke(wait_until_t{}, std::forward<S>(scheduler),
                      std::forward<Args>(args)...);
  }
};
inline constexpr wait_until_t wait_until{};

class glib_io_context;

struct schedule_sender;
struct wait_for_sender;
struct wait_until_sender;

enum class when { readable = 1, writable = 2, error = 4 };
auto operator|(when, when) noexcept -> when;
auto operator&(when, when) noexcept -> bool;

class glib_scheduler {
  friend class schedule_sender;

  auto get_GMainContext() const noexcept -> ::GMainContext*;

  friend auto tag_invoke(stdexec::schedule_t, glib_scheduler self) noexcept
      -> schedule_sender;

  friend auto tag_invoke(wait_for_t, glib_scheduler self,
                         std::chrono::milliseconds dur) noexcept
      -> wait_for_sender;

  friend auto tag_invoke(wait_until_t, glib_scheduler self, int fd,
                         when condition) noexcept -> wait_until_sender;

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
  struct Source : ::GSource {
    Receiver receiver_;
    ::GMainContext* context_;
  };

  inline static ::GSourceFuncs vtable_{
      [](::GSource* self, int* timeout) -> gboolean {
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
      }, // dispatch
      [](::GSource* source) {
        auto* self = static_cast<Source*>(source);
        self->receiver_.~Receiver();
      } // finalize
  };

  Source* source_;

  friend auto tag_invoke(stdexec::start_t, schedule_operation& self) noexcept
      -> void {
    ::g_source_set_callback(
        self.source_,
        [](gpointer data) -> gboolean {
          auto* self = static_cast<Source*>(data);
          try {
            stdexec::set_value(std::move(self->receiver_));
          } catch (...) {
            stdexec::set_error(std::move(self->receiver_),
                               std::current_exception());
          }
          return G_SOURCE_REMOVE;
        },
        self.source_, nullptr);
    ::g_source_attach(self.source_, self.source_->context_);
    ::g_source_unref(std::exchange(self.source_, nullptr));
  }

public:
  schedule_operation(::GMainContext* context, Receiver&& receiver) {
    source_ =
        reinterpret_cast<Source*>(::g_source_new(&vtable_, sizeof(Source)));
    new (&source_->receiver_) Receiver{std::move(receiver)};
    source_->context_ = context;
  }
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
    return {self.scheduler_.context_->context_.get(),
            std::forward<R>(receiver)};
  }

  friend attrs tag_invoke(stdexec::get_attrs_t,
                          const schedule_sender& self) noexcept {
    return attrs{self.scheduler_};
  }

  friend void tag_invoke(stdexec::start_detached_t, const schedule_sender& self,
                         stdexec::__empty_env) noexcept {
    schedule_operation op{self.get_GMainContext(),
                          stdexec::__start_detached::__detached_receiver_t<
                              stdexec::__empty_env>{}};
    stdexec::start(op);
  }
};

static_assert(stdexec::sender<schedule_sender>);
static_assert(stdexec::scheduler<glib_scheduler>);

template <typename Receiver>
requires stdexec::receiver<Receiver>
struct glib_io_context::async_timeout_operation {
  ::GMainContext* context_{nullptr};
  std::chrono::milliseconds timeout_{};
  Receiver receiver_{};

  friend auto tag_invoke(stdexec::start_t, async_timeout_operation& op) noexcept
      -> void {
    GSource* source = ::g_timeout_source_new(op.timeout_.count());
    ::g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          auto* op = static_cast<async_timeout_operation*>(data);
          stdexec::set_value((Receiver &&) op->receiver_);
          return G_SOURCE_REMOVE;
        },
        &op, nullptr);
    ::g_source_attach(source, op.context_);
    ::g_source_unref(source);
  }
};

struct glib_io_context::async_timeout_sender {
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(),
                                     stdexec::set_error_t(std::exception_ptr),
                                     stdexec::set_stopped_t()>;

  ::GMainContext* context_{nullptr};
  std::chrono::milliseconds timeout_{};

  template <typename Receiver>
  requires stdexec::receiver<Receiver>
  friend auto tag_invoke(stdexec::connect_t,
                         glib_io_context::async_timeout_sender& sender,
                         Receiver&& receiver)
      -> glib_io_context::async_timeout_operation<Receiver> {
    using ReceiverType = std::remove_cvref_t<Receiver>;
    return async_timeout_operation<ReceiverType>{
        sender.context_, sender.timeout_, std::forward<Receiver>(receiver)};
  }
};

template <typename Receiver> struct glib_io_context::async_read_operation {
  ::GMainContext* context_{nullptr};
  int fd_{};
  std::span<char> buffer_{};
  Receiver receiver_{};
  inline static GSourceFuncs vtable_{
      nullptr, // prepare
      nullptr, // check
      [](::GSource*, ::GSourceFunc callback, gpointer data) -> gboolean {
        return callback(data);
      },       // dispatch
      nullptr, // finalize
  };

  friend auto tag_invoke(stdexec::start_t, async_read_operation& op) noexcept
      -> void {
    GSource* source = ::g_source_new(&vtable_, sizeof(GSource));
    ::g_source_add_unix_fd(source, op.fd_,
                           (GIOCondition)(G_IO_IN | G_IO_ERR | G_IO_HUP));
    ::g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          auto* op = static_cast<async_read_operation*>(data);
          errno = 0;
          ssize_t bytes_read =
              ::read(op->fd_, op->buffer_.data(), op->buffer_.size_bytes());
          std::error_code ec(errno, std::system_category());
          if (bytes_read < 0) {
            stdexec::set_error((Receiver &&) op->receiver_,
                               std::make_exception_ptr(std::system_error(ec)));
          } else {
            stdexec::set_value((Receiver &&) op->receiver_, ec,
                               static_cast<std::size_t>(bytes_read));
          }
          return G_SOURCE_REMOVE;
        },
        &op, nullptr);
    ::g_source_attach(source, op.context_);
    ::g_source_unref(source);
  }
};

struct glib_io_context::async_read_sender {
  using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::error_code, std::size_t),
      stdexec::set_error_t(std::exception_ptr), stdexec::set_stopped_t()>;

  ::GMainContext* context_{nullptr};
  int fd_{};
  std::span<char> buffer_{};

  template <typename Receiver>
  requires stdexec::receiver<Receiver>
  friend auto tag_invoke(stdexec::connect_t,
                         glib_io_context::async_read_sender& sender,
                         Receiver&& receiver) {
    using ReceiverType = std::remove_cvref_t<Receiver>;
    return async_read_operation<ReceiverType>{sender.context_, sender.fd_,
                                              sender.buffer_,
                                              std::forward<Receiver>(receiver)};
  }
};

} // namespace gsenders

#endif