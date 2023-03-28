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

template <std::integral Int, class Receiver> struct enumerate_operation_base {
  [[no_unique_address]] Receiver rcvr_;
  std::atomic<Int> count_{};

  template <stdexec::__decays_to<Receiver> Rcvr>
  enumerate_operation_base(Rcvr&& rcvr, I initial_val) noexcept(
      std::is_nothrow_constructible_v<Receiver, Rcvr>)
      : rcvr_{static_cast<Rcvr&&>(rcvr)}, count_{initial_val} {}
};

template <std::integral Int, class Receiver> class enumerate_receiver_t {
public:
  explicit enumerate_receiver_t(
      enumerate_operation_base<Int, Receiver>& op) noexcept
      : op_(op) {}

private:
  template <class... Args>
  friend void tag_invoke(stdexec::set_value_t, enumerate_receiver_t&& self,
                         Args&&... args) noexcept {
    stdexec::set_value(static_cast<Receiver&&>(self.op_.rcvr_), self.op_.count_,
                       static_cast<Args&&>(args)...);
  }

  template <stdexec::sender Item>
  friend auto tag_invoke(set_next_t, enumerate_receiver_t& self,
                         Item&& item) noexcept {
    return stdexec::let_value(
        static_cast<Item&&>(item), [&self]<class... Args>(Args&&... args) {
          I count = self.op_.count_.fetch_add(1, std::memory_order_relaxed);
          return stdexec::just(count, static_cast<Args&&>(args)...);
        });
  }

  template <class _Error>
  friend void tag_invoke(stdexec::set_error_t, enumerate_receiver_t&& self,
                         _Error&& error) noexcept {
    stdexec::set_error((_Receiver&&)self.op_.rcvr_, (_Error&&)error);
  }

  friend void tag_invoke(stdexec::set_stopped_t,
                         enumerate_receiver_t&& self) noexcept {
    stdexec::set_value((_Receiver&&)self.op_.rcvr_, self.op_.count_);
  }

  enumerate_operation_base& op_;
};

template <std::integral Int, class Sender, class Receiver>
struct enumerate_operation : enumerate_operation_base<I, Receiver> {
  stdexec::connect_result_t<Sender, enumerate_receiver_t<Int, Receiver>> op_;

  template <stdexec::__decays_to<Sender> Sndr,
            stdexec::__decays_to<Receiver> Rcvr>
  enumerate_operation(Sndr&& sndr, Rcvr&& rcvr, Int initial_val)
      : enumerate_operation_base(static_cast<Rcvr&&>(rcvr), initial_val),
        op_(stdexec::connect(sndr, receiver_t{*this})) {}

private:
  friend void tag_invoke(stdexec::start_t, enumerate_operation& self) noexcept {
    stdexec::start(self.op_);
  }
};

template <std::integral Int, class Sender>
struct enumerate_sender {
  [[no_unique_address]] Sender sndr_;
  Int initial_val_;

  template <stdexec::__decays_to<Sender> Sndr>
  enumerate_sender(Sndr&& sndr, Int initial_val = Int{}) noexcept(
      std::is_nothrow_constructible_v<Sender, Sndr>)
      : sndr_{static_cast<Sndr&&>(sndr)}, initial_val_{initial_val} {}

  template <stdexec::receiver Rcvr>
  requires next_receiver_of<Int, Rcvr>
  friend auto tag_invoke(stdexec::connect_t, enumerate_sender&& self,
                         Rcvr&& rcvr) {
    return enumerate_operation<Int, Sender, std::decay_t<Rcvr>>{
        static_cast<Sender&&>(self.sndr_),
        static_cast<Rcvr&&>(rcvr), self.initial_val_};
  }
};

struct enumerate_t {
  template <std::integral Int, class Sender>
    requires stdexec::sender<Sender>
  auto operator()(Sender&& sndr, Int initial_val = Int{}) const noexcept {
    return enumerate_sender<Int, std::decay_t<Sender>>{
        static_cast<Sender&&>(sndr), initial_val};
  }
};

inline constexpr enumerate_t enumerate{};

template <class Receiver>
struct repeat_next_operation {
  [[no_unique_address]] Receiver rcvr_;

  template <stdexec::__decays_to<Receiver> Rcvr>
  repeat_next_operation(Rcvr&& rcvr) noexcept(
      std::is_nothrow_constructible_v<Receiver, Rcvr>)
      : rcvr_{static_cast<Rcvr&&>(rcvr)} {}
};

} // namespace gsenders