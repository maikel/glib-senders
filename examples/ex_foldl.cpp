#include "glib-senders/foldl.hpp"
#include "glib-senders/completes_if.hpp"

using namespace stdexec;

template <class Sender>
auto repeat(Sender&& sender) {
  return foldl((Sender &&) sender, just(), [](auto&&) { return just(); });
}

template <class Sender>
auto repeat_n(Sender&& sender, int n) {
  using exec::completes_if;
  return foldl((Sender &&) sender, just(0),
      [n](auto&&, int i) {
        return when_any(
          completes_if(i < n) 
          | then([i] { return i + 1; }),
          completes_if(i >= n)
          | let_value([] { return just_stopped(); })
        );
      });
}

int main()
{
  auto snd = repeat_n(just(1), 10);
}