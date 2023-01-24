#ifndef GLIB_SENDERS_GLIB_IO_CONTEXT_HPP
#define GLIB_SENDERS_GLIB_IO_CONTEXT_HPP

#include <span>
#include <stdexec/execution.hpp>

#include "glib.h"

namespace gsenders {
class glib_io_context {
public:
  glib_io_context();
  explicit glib_io_context(::GMainContext* context);

  glib_io_context(const glib_io_context&) = delete;
  glib_io_context& operator=(const glib_io_context&) = delete;

  glib_io_context(glib_io_context&&) = delete;
  glib_io_context& operator=(glib_io_context&&) = delete;

  template <typename Receiver> struct async_timeout_operation;
  struct async_timeout_sender;

  [[nodiscard]] auto async_wait_for(std::chrono::milliseconds timeout)
      -> async_timeout_sender;

  template <typename Receiver> struct async_read_operation;
  struct async_read_sender;

  [[nodiscard]] auto async_read_some(int fd, std::span<char> buffer)
      -> async_read_sender;

  auto run() -> void;

  auto stop() -> void;

private:
  struct context_destroy {
    void operator()(::GMainContext* pointer) const noexcept;
  };
  struct loop_destroy {
    void operator()(::GMainLoop* pointer) const noexcept;
  };
  std::unique_ptr<::GMainContext, context_destroy> context_{nullptr};
  std::unique_ptr<::GMainLoop, loop_destroy> loop_{nullptr};
};

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
  GSourceFuncs vtable_{
      nullptr,  // prepare
      nullptr,  // check
      [](::GSource*, ::GSourceFunc callback, gpointer data) -> gboolean {
        return callback(data);
      },  // dispatch
      nullptr,  // finalize
      nullptr,  // closure_callback
      nullptr   // closure_marshall
  };

  friend auto tag_invoke(stdexec::start_t, async_read_operation& op) noexcept
      -> void {
    GSource* source = ::g_source_new(&op.vtable_, sizeof(GSource));
    ::g_source_add_unix_fd(source, op.fd_, (GIOCondition)(G_IO_IN | G_IO_ERR | G_IO_HUP));
    ::g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          auto* op = static_cast<async_read_operation*>(data);
          ssize_t bytes_read =
              ::read(op->fd_, op->buffer_.data(), op->buffer_.size_bytes());
          if (bytes_read == -1) {
            stdexec::set_error((Receiver &&) op->receiver_,
                               std::make_exception_ptr(std::system_error(
                                   errno, std::system_category())));
          } else if (bytes_read == 0) {
            stdexec::set_stopped((Receiver &&) op->receiver_);
          } else {
            stdexec::set_value((Receiver &&) op->receiver_, bytes_read);
          }
          return G_SOURCE_REMOVE;
        },
        &op, nullptr);
    ::g_source_attach(source, op.context_);
    ::g_source_unref(source);
  }
};

struct glib_io_context::async_read_sender {
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(std::size_t),
                                     stdexec::set_error_t(std::exception_ptr),
                                     stdexec::set_stopped_t()>;

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
