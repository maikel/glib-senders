#ifndef GLIB_SENDERS_ASYNC_IO_STREAM_HPP
#define GLIB_SENDERS_ASYNC_IO_STREAM_HPP

#include <stdexec/execution.hpp>

#include <span>

namespace gsenders
{

struct async_read_some_t {
  template <stdexec::sender Sender, typename... Ts>
  requires stdexec::tag_invocable<async_read_some_t, Sender, Ts...>
  auto operator()(Sender&& sender, Ts&&... args) const noexcept(
      stdexec::nothrow_tag_invocable<async_read_some_t, Sender, Ts...>) {
    return tag_invoke(async_read_some_t{}, (Sender &&) sender,
                      ((Ts &&) args)...);
  }

  template <class... Args>
  stdexec::__binder_back<async_read_some_t, std::remove_cvref_t<Args>...>
  operator()(Args&&... args) const {
    return {{}, {}, {(Args &&) args...}};
  }
};
inline constexpr async_read_some_t async_read_some;

template <typename Scheduler>
class async_io_stream {
public:
  async_io_stream(Scheduler s, int fd);

  auto async_read_some(std::span<char> buffer);
  auto async_write_some(std::span<const char> buffer);

private:
  Scheduler scheduler_;
  int fd_;
};

}

#endif