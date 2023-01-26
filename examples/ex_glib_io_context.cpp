#include "glib-senders/glib_io_context.hpp"
#include "glib-senders/repeat_until.hpp"
#include <iostream>

template <typename Scheduler> struct basic_file_descriptor {
  Scheduler scheduler_;
  int fd_;
};

using file_descriptor = basic_file_descriptor<gsenders::glib_scheduler>;

auto async_read(file_descriptor fd, std::span<char> buffer) {
  return gsenders::wait_until(fd.scheduler_, fd.fd_,
                              gsenders::io_condition::is_readable) |
         stdexec::then([buffer](int fd) {
           ssize_t nbytes = ::read(fd, buffer.data(), buffer.size());
           if (nbytes == -1) {
             throw std::system_error(errno, std::system_category());
           }
           return buffer.subspan(0, nbytes);
         });
}

int main() {
  gsenders::glib_io_context io_context{};
  gsenders::glib_scheduler scheduler = io_context.get_scheduler();

  int i = 0;

  auto schedule = stdexec::schedule(scheduler);
  auto add_one = schedule | stdexec::then([&i] { i += 1; });

  auto wait_a_second =
      gsenders::wait_for(scheduler, std::chrono::seconds(1));

  auto wait_two_seconds =
      gsenders::wait_for(scheduler, std::chrono::seconds(2));

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
  auto n_is_bigger_than_10 = async_read(fd, buffer) //
                             | stdexec::then([&n](std::span<char> buf) {
                                 std::string_view sv(buf.data(), buf.size());
                                 n += buf.size();
                                 std::cout << n << ": " << sv;
                               }) //
                             | stdexec::then([&n] { return n > 10; });

  using gsenders::repeat_until;
  stdexec::start_detached(repeat_until(n_is_bigger_than_10) | then_stop);

  io_context.run();
}