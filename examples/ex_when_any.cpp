#include "glib-senders/file_descriptor.hpp"
#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/when_any.hpp"

int main() {
  using namespace gsenders;
  using namespace std::chrono_literals;

  file_descriptor fd{STDIN_FILENO};
  char buffer[128];

  stdexec::start_detached( //
      when_any(stdexec::just()));

  glib_io_context{}.run();
}