#include "glib-senders/file_descriptor.hpp"
#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/when_any.hpp"

#include <iostream>

int main() {
  using namespace gsenders;
  using namespace std::chrono_literals;

  glib_io_context context{};
  file_descriptor fd{STDIN_FILENO};
  char buffer[128];

  stdexec::start_detached( //
      when_any(async_read_some(fd, buffer), wait_for(10s)) //
      | stdexec::then([&context](auto&&... args) {
        if constexpr (sizeof...(args) == 0) {
          std::cout << "Timeout!\n";
        } else {
          std::cout << "Read " << std::get<0>(std::tuple{args...}).size()
                    << " bytes\n";
        }
        context.stop();
      }));

  context.run();
}