Senders and Receivers for the GNOME Glib library 
===============================================

This is a small C++ library that glues together the Glib event loop and the current executor proposal for C++.
It can be used to compose asynchronous tasks in a Glib event loop.

Since single-valued senders can be automatically converted to awaitable objects, the library also provides a way to use C++ coroutines in a Glib event loop.

The library requires C++20.

Example

```cpp
#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/file_descriptor.hpp"

#include <iostream>
#include <exec/task.hpp>

using namespace gsenders;

template <typename S, typename T>
concept sender_of = stdexec::sender_of<S, stdexec::set_value_t(T)>;

template <sender_of<file_descriptor> S>
exec::task<void> echo(S get_fd) {
  file_descriptor fd = co_await std::move(get_fd);
  char buffer[1024];
  int n = 0;
  while (n < 10) {
    std::span<char> input = co_await async_read_some(fd, buffer);
    n += input.size();
    std::string_view sv(input.data(), input.size());
    std::cout << n << ": " << sv;
  }
} 

int main() {
  glib_io_context io_context{};
  auto get_fd = stdexec::just(file_descriptor{io_context.get_scheduler(), STDIN_FILENO});
  auto then_stop = stdexec::then([&] { io_context.stop(); });
  stdexec::start_detached(echo(get_fd) | then_stop);
  io_context.run();
}
```
