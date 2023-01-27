#include "glib-senders/file_descriptor.hpp"
#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/repeat_until.hpp"
#include <iostream>

using namespace gsenders;

template <typename Scheduler> struct basic_file_descriptor {
  Scheduler scheduler_;
  int fd_;
};

int main() {
  glib_io_context io_context{};
  glib_scheduler scheduler = io_context.get_scheduler();

  int i = 0;

  using namespace std::literals::chrono_literals;

  auto schedule = stdexec::schedule(scheduler);
  auto add_one = schedule | stdexec::then([&i] { i += 1; });

  auto wait_a_second = wait_for(scheduler, 1s);

  auto wait_two_seconds = wait_for(scheduler, 2s);

  auto and_just_stop =
      stdexec::let_value([] { return stdexec::just_stopped(); });

  stdexec::start_detached(
      stdexec::when_all(
          wait_a_second                                        //
              | stdexec::then([] { std::cout << "Hello!\n"; }) //
              | and_just_stop,
          wait_two_seconds                                             //
              | stdexec::upon_stopped([] { std::cout << "Stop!\n"; })) //
      | stdexec::upon_stopped([&] { io_context.stop(); }));

  io_context.run();

  auto say_hello = wait_a_second |
                   stdexec::then([] { std::cout << "Hello!\n"; }) |
                   stdexec::let_value([=] { return wait_a_second; }) |
                   stdexec::then([] { std::cout << "World!\n"; });

  auto then_stop = stdexec::then([&io_context] { io_context.stop(); });

  stdexec::start_detached(stdexec::when_all(add_one, add_one, say_hello) |
                          then_stop);

  assert(i == 0);

  io_context.run();

  assert(i == 2);

  file_descriptor fd{scheduler, STDIN_FILENO};
  char buffer[1024];

  int n = 0;
  auto n_is_bigger_than_10 = async_read_some(fd, buffer) //
                             | stdexec::then([&n](std::span<char> buf) {
                                 std::string_view sv(buf.data(), buf.size());
                                 n += buf.size();
                                 std::cout << n << ": " << sv;
                               }) //
                             | stdexec::then([&n] { return n > 10; });

  auto read_input = repeat_until(n_is_bigger_than_10) |
                    stdexec::then([] { std::cout << "You win.\n"; }) |
                    and_just_stop;
  auto timeout = wait_for(scheduler, 10s) |
                 stdexec::then([] { std::cout << "You lose.\n"; }) |
                 and_just_stop;

  auto timed_read = stdexec::when_all(read_input, timeout) //
                    | stdexec::upon_stopped([] {})         //
                    | then_stop;

  stdexec::start_detached(timed_read);

  io_context.run();
}