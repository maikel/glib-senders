#include <glib-senders/repeat_effect.hpp>
#include <glib-senders/join_all.hpp>
#include <glib-senders/let_value_each.hpp>
#include <glib-senders/then_each.hpp>
#include <glib-senders/enumerate.hpp>

#include <exec/variant_sender.hpp>

#include <iostream>

using namespace gsenders;

using just_stopped_t = decltype(stdexec::just_stopped());
using just_t = decltype(stdexec::just(0));

int main() {
  stdexec::sync_wait(
    repeat_effect(stdexec::just()) //
    | enumerate()                  //
    | let_value_each([](int n) -> exec::variant_sender<just_stopped_t, just_t> {
        if (n < 10) {
          return stdexec::just(n);
        }
        return stdexec::just_stopped();
      })                                               //
    | then_each([](int n) { std::cout << n << '\n'; }) //
    | join_all()                                       //
    | stdexec::then([] { std::cout << "done.\n"; }));
}