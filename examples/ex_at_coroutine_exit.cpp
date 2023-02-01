#include "glib-senders/at_coroutine_exit.hpp"

#include <exec/task.hpp>
#include <iostream>

using namespace gsenders;

exec::task<void> say_hello() {
  co_await at_coroutine_exit([]() -> exec::task<void> {
    std::cout << "!\n";
    co_return;
  });
  co_await at_coroutine_exit([]() -> exec::task<void> {
    std::cout << "World";
    co_return;
  });
  std::cout << "Hello, ";
}

int main() { stdexec::sync_wait(say_hello()); }