#include <glib-senders/repeat_effect.hpp>
#include <glib-senders/sequence_join.hpp>
#include <glib-senders/let_value_each.hpp>

#include <exec/variant_sender.hpp>

using namespace gsenders;

using just_stopped_t = decltype(stdexec::just_stopped());
using just_t = decltype(stdexec::just());

int main() {
  stdexec::sync_wait(sequence_join(repeat_effect(stdexec::just())));
}