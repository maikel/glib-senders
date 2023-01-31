#include "glib-senders/file_descriptor.hpp"
#include "glib-senders/sequence.hpp"
#include "glib-senders/glib_io_context.hpp"

#include <array>
#include <iostream>
#include <ranges>

int main() {
  using namespace gsenders;
  using namespace std::chrono_literals;

  glib_io_context context{};
  std::thread runner{[&] { context.run(); }};

  file_descriptor fd1{STDIN_FILENO};
  file_descriptor fd2{STDIN_FILENO};
  char buffer1[128];
  char buffer2[128];

  {
    enum class operation_kind { read, timeout };
    // sequence returns a stream of senders
    auto queue = sequence(async_read_some(fd1, buffer1), wait_for(10s));
    for (stdexec::sender auto work : queue) {
      auto result = stdexec::sync_wait(std::move(work) | stdexec::then([](auto... args) -> int {
        if constexpr (sizeof...(args)) {
          return 1;
        }
        return 0;
      }));
      if (result && std::get<0>(*result)) {
        std::cout << "Read!\n";
        break;
      } else {
        std::cout << "Timeout!\n";
      }
    }
  }

  context.stop();
  runner.join();
}