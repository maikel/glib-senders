#include "glib-senders/at_coroutine_exit.hpp"
#include "glib-senders/file_descriptor.hpp"
#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/sequence.hpp"

#include <array>
#include <iostream>
#include <ranges>

using namespace gsenders;
using namespace std::chrono_literals;

template <class... Senders>
exec::task<void> play(sequence_::stream<Senders...>& seq) {
  co_await at_coroutine_exit(
      [](auto seq) -> exec::task<void> { co_await seq->cleanup(); }, &seq);
  for (stdexec::sender auto work : seq) {
    auto result =
        co_await stdexec::then(std::move(work), [](auto... args) -> int {
          if constexpr (sizeof...(args)) {
            return 1;
          }
          return 0;
        });
    if (result) {
      std::cout << "STDIN\n";
    } else {
      std::cout << "Timeout\n";
    }
    break;
  }
}

exec::task<void> play(file_descriptor fd, std::span<char> buffer) {
  std::cout << "Press enter or wait 2 seconds to exit\n";
  sequence_::stream seq = sequence(async_read_some(fd, buffer), wait_for(2s));
  co_await play(seq);
}

int main() {
  glib_io_context context{};

  file_descriptor fd{STDIN_FILENO};
  char buffer[128];
  stdexec::start_detached(
      play(fd, buffer)                                              //
      | stdexec::then([&context] { return context.stop(); }) //
      | stdexec::upon_stopped([] { std::cout << "Stopped\n"; }));

  context.run();
}