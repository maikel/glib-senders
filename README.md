![CMake Status](https://github.com/maikel/glib-senders/actions/workflows/cmake.yml/badge.svg)

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

#include <exec/task.hpp>

using namespace gsenders;

exec::task<void> write(file_descriptor fd, std::span<const char> buffer) {
  while (!buffer.empty()) {
    buffer = co_await async_write_some(fd, buffer);
  }
} 

exec::task<void> echo(file_descriptor in, file_descriptor out) {
  char buffer[1024];
  int n = 0;
  for (int n = 0; n < 10; ++n) {
    std::span<char> received = co_await async_read_some(in, buffer);
    co_await write(out, received);
  }
} 

int main() {
  file_descriptor in{STDIN_FILENO};
  file_descriptor out{STDOUT_FILENO};
  auto then_stop = stdexec::then([] { glib_io_context().stop(); });
  stdexec::start_detached(echo(in, out) | then_stop);
  glib_io_context().run();
}
```
