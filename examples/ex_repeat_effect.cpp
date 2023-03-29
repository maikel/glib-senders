#include <glib-senders/repeat_each.hpp>

using namespace gsenders;

int main()
{
  auto r = repeat_each(stdexec::just());
}