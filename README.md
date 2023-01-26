Senders and Receiver for the GNOME Glib library 
===============================================

This is a small C++ library that glues together the Glib event loop and the current executor proposal for C++.
It can be used to compose asynchronous tasks in a Glib event loop.

Since single-valued senders can be automatically converted to awaitable objects, the library also provides a way to use C++ coroutines in a Glib event loop.

The library requires C++20.

Example

```cpp
template <stdexec::sender_of<file_descriptor> S>
exec::task<void> echo(S get_fd) {
  file_descriptor fd = co_await std::move(get_fd);
  char buffer[1024];
  int n = 0;
  while (n < 10) {
    std::span<char> input = co_await async_read_some(fd, buffer);
    std::string_view sv(input.data(), input.size());
    std::cout << n << ": " << sv;
    n += input.size();
  }
}

int main() {
  glib_io_context io_context{};
  file_descriptor fd{io_context, STDIN_FILENO};
  stdexec::start_detached(echo(stdexec::just(fd)) |
                          stdexec::then([&] { io_context.stop(); }));
  io_context.run();
}
```