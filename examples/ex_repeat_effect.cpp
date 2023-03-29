#include <glib-senders/repeat_effect.hpp>
#include <glib-senders/sequence_join.hpp>
#include <glib-senders/let_value_each.hpp>
#include <glib-senders/enumerate.hpp>

#include <exec/variant_sender.hpp>

#include <iostream>

using namespace gsenders;

using just_stopped_t = decltype(stdexec::just_stopped());
using just_t = decltype(stdexec::just());

int main() {
  stdexec::sync_wait(
    repeat_effect(stdexec::just()) //
    | enumerate()                  //
    | let_value_each([](int n) -> exec::variant_sender<just_stopped_t, just_t> {
        std::cout << n << std::endl;
        if (n < 10) {
          return stdexec::just();
        }
        return stdexec::just_stopped();
      }) //
    | sequence_join()
    | stdexec::then([] { std::cout << "done.\n"; }));
}