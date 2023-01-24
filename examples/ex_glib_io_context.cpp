#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/safe_file_descriptor.hpp"
#include <iostream>
#include <mutex>

int main() {
  std::mutex mutex;
  gsenders::glib_io_context io_context{};
  std::thread t{[&] {
    std::cout << "[" << std::this_thread::get_id() << "] ";
    std::cout << "Run Context...\n";
    io_context.run();
    std::cout << "[" << std::this_thread::get_id() << "] ";
    std::cout << "Stopped Context...\n";
  }};
  auto start = std::chrono::steady_clock::now();
  stdexec::sender auto sender =
      io_context.async_wait_for(std::chrono::milliseconds{1000}) | stdexec::then([&] {
    std::cout << "[" << std::this_thread::get_id() << "] ";
    std::cout << "Timer went off!\n";
    auto stop = std::chrono::steady_clock::now();
    auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    std::cout << "[" << std::this_thread::get_id() << "] ";
    std::cout << "Time elapsed: " << diff.count() << "ms.\n";
  });
  stdexec::sync_wait(sender);

  int fd{STDIN_FILENO};
  std::array<char, 1024> buffer{};
  stdexec::sender auto sender2 =
      io_context.async_read_some(fd, buffer) | stdexec::then([&](auto result) {
    std::cout << "[" << std::this_thread::get_id() << "] ";
    std::cout << "Read " << result << " bytes.\n";
    std::cout << "[" << std::this_thread::get_id() << "] ";
    std::cout << "Read: " << buffer.data() << "\n";
  });
  stdexec::sync_wait(sender2);

  io_context.stop();
  t.join();
}