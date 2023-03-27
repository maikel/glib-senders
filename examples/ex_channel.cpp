#include "glib-senders/channel.hpp"
#include <iostream>

int main()
{
  using namespace stdexec;
  gsenders::channel<int> channel;
  sender_of<set_value_t()> auto send = channel.send(42);
  sender_of<set_value_t(int&&)> auto recv = channel.receive();
  auto [i] = stdexec::sync_wait(stdexec::when_all(send, recv)).value();
}