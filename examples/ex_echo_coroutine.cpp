#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/file_descriptor.hpp"

#include <iostream>
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
  glib_io_context io_context{::g_main_context_default()};
  file_descriptor in{io_context.get_scheduler(), STDIN_FILENO};
  file_descriptor out{io_context.get_scheduler(), STDOUT_FILENO};
  auto then_stop = stdexec::then([&] { io_context.stop(); });
  stdexec::start_detached(echo(in, out) | then_stop);
  io_context.run();
}