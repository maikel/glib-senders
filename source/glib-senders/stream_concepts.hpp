#pragma once

#include <stdexec/execution.hpp>

namespace gsenders {

struct set_next_t {
  template <stdexec::receiver _Receiver, stdexec::sender _Item>
  requires stdexec::tag_invocable<set_next_t, _Receiver&, _Item> &&
           stdexec::sender<
               stdexec::tag_invoke_result_t<set_next_t, _Receiver&, _Item>>
  auto operator()(_Receiver& __rcvr, _Item&& __item) const noexcept
      -> stdexec::tag_invoke_result_t<set_next_t, _Receiver&, _Item> {
    static_assert(
        stdexec::nothrow_tag_invocable<set_next_t, _Receiver&, _Item>);
    return tag_invoke(*this, __rcvr, (_Item&&)__item);
  }
};

inline constexpr set_next_t set_next{};

template <class Receiver, class Item>
concept next_receiver_of =
    stdexec::receiver<Receiver> && requires(Receiver& rcvr, Item&& item) {
      { set_next(rcvr, (Item&&)item) } -> stdexec::sender;
    };

} // namespace gsenders