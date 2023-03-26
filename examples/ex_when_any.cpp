#include "glib-senders/file_descriptor.hpp"
#include "glib-senders/glib_io_context.hpp"

#include <exec/when_any.hpp>
#include <exec/timed_scheduler.hpp>

#include <iostream>

namespace ex = stdexec;

int main() {
  using namespace gsenders;
  using namespace std::chrono_literals;

  glib_io_context context{};
  glib_scheduler sch = context.get_scheduler();
  file_descriptor fd{STDIN_FILENO};
  char buffer[128];

  static_assert(ex::scheduler<glib_scheduler>);

  ex::start_detached( //
      exec::when_any(async_read_some(fd, buffer), exec::schedule_after(sch, 1s) | ex::then([] { return std::span<char>{}; })) //
      | ex::then([&context](std::span<char> span) {
        if (span.size() > 0) {
          std::cout << "Read " << span.size() << " bytes\n";
        } else {
          std::cout << "Timeout\n";
        }
        context.stop();
      }));

  context.run();
}