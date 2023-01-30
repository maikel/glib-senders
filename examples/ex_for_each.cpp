#include "glib-senders/file_descriptor.hpp"
#include "glib-senders/for_each.hpp"
#include "glib-senders/glib_io_context.hpp"

#include <array>
#include <iostream>
#include <ranges>

int main() {
  using namespace gsenders;
  using namespace std::chrono_literals;

  glib_io_context context{};
  file_descriptor fd{STDIN_FILENO};
  char buffer[128];

  auto work = for_each(async_read_some(fd, buffer), wait_for(10s)) //
              | std::ranges::view::transform([](stdexec::sender auto&& result) {
                  return std::move(result) | stdexec::then([](auto&&... args) {
                           if constexpr (sizeof...(args) == 0) {
                             std::cout << "Timeout!\n";
                           } else {
                             std::cout
                                 << "Read "
                                 << std::get<0>(std::tuple{args...}).size()
                                 << " bytes\n";
                           }
                         });
                });

  std::apply(
      [&](auto&&... items) {
        stdexec::start_detached(when_all(std::move(items)...) |
                                stdexec::then([&] { context.stop(); }));
      },
      std::move(work));

  context.run();
}