#include "glib-senders/channel.hpp"
#include <exec/task.hpp>

#include <iostream>

exec::task<void> sender(gsenders::channel<int>& ch) {
  for (int i = 0; i < 10; ++i) {
    co_await ch.send(i);
    std::cout << "Ping " << i << '\n';
  }
}

exec::task<void> receiver(gsenders::channel<int>& ch) {
  for (int i = 0; i < 10; ++i) {
    int n = co_await ch.receive();
    std::cout << "Pong " << n << '\n';
  }
}

int main()
{
  gsenders::glib_io_context ctx{};
  std::thread runner{[&] { ctx.run(); }};
  gsenders::channel<int> ch;
  
  stdexec::sync_wait(stdexec::when_all(sender(ch), receiver(ch)));

  ctx.stop();
  runner.join();
}