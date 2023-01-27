#ifndef GLIB_SENDERS_RETRY_HPP
#define GLIB_SENDERS_RETRY_HPP

#include <concepts>
#include <stdexec/execution.hpp>

namespace gsenders {
namespace detail {
// _conv needed so we can emplace construct non-movable types into
// a std::optional.
template <std::invocable F>
requires std::is_nothrow_move_constructible_v<F>
struct _conv {
  F f_;
  explicit _conv(F f) noexcept : f_((F &&) f) {}
  operator std::invoke_result_t<F>() && { return ((F &&) f_)(); }
};

template <typename Sender, typename Receiver>
struct repeat_operation;

template <typename Sender, typename Receiver>
struct repeat_receiver : stdexec::receiver_adaptor<repeat_receiver<Sender, Receiver>> {
  repeat_operation<Sender, Receiver>* op_;

  explicit repeat_receiver(repeat_operation<Sender, Receiver>& op) noexcept
      : op_{&op} {}

  Receiver&& base() && noexcept { return std::move(op_->receiver_); }

  const Receiver& base() const& noexcept { return op_->receiver_; }

  auto set_value(bool stop) && noexcept -> void try {
    if (!stop) {
      op_->repeat();
    } else {
      stdexec::set_value(std::move(op_->receiver_));
    }
  } catch (...) {
    stdexec::set_error(std::move(op_->receiver_), std::current_exception());
  }
};

template <typename Sender, typename Receiver>
struct repeat_operation {
  Sender sender_;
  Receiver receiver_;
  std::optional<
      stdexec::connect_result_t<Sender, repeat_receiver<Sender, Receiver>>>
      op_;

  repeat_operation(Sender s, Receiver r)
      : sender_(std::move(s)), receiver_(std::move(r)), op_{connect()} {}

  auto connect() noexcept {
    return _conv{[this] {
      return stdexec::connect(std::move(sender_),
                              repeat_receiver<Sender, Receiver>{*this});
    }};
  }

  auto repeat() noexcept -> void try {
    op_.emplace(connect());
    stdexec::start(*op_);
  } catch (...) {
    stdexec::set_error(std::move(receiver_), std::current_exception());
  }

  friend void tag_invoke(stdexec::start_t, repeat_operation& self) noexcept {
    stdexec::start(*self.op_);
  }
};

template <typename Sender> struct repeat_sender {
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(),
                                     stdexec::set_error_t(std::exception_ptr),
                                     stdexec::set_stopped_t()>;

  Sender sender_;

  template <typename Receiver>
  requires stdexec::sender_to<Sender, repeat_receiver<Sender, std::remove_cvref_t<Receiver>>>
  friend auto tag_invoke(stdexec::connect_t, repeat_sender self,
                         Receiver&& r) noexcept {
    return repeat_operation<Sender, std::remove_cvref_t<Receiver>>{
        std::move(self).sender_, std::forward<Receiver>(r)};
  }

  template <typename Self>
  requires stdexec::__decays_to<Self, repeat_sender>
  friend auto tag_invoke(stdexec::get_attrs_t, Self&& self) {
    return tag_invoke(stdexec::get_attrs_t{}, self.sender_);
  }
};

struct repeat_until_t {

  // @brief Schedule an input sender until it returns a boolean value or true.
  //
  // @param[in] condition  A sender object that completes with a boolean value
  //
  // @return Returns a sender object that completes without a value
  template <class S>
  requires stdexec::sender<S>
  auto operator()(S&& condition) const -> repeat_sender<std::remove_cvref_t<S>> {
    return {std::forward<S>(condition)};
  }
};
} // namespace detail

// @brief Schedule an input sender until it returns a boolean value or true.
//
// @param[in] condition  A sender object that completes with a boolean value
//
// @return Returns a sender object that completes without a value
inline constexpr detail::repeat_until_t repeat_until{};

} // namespace gsenders

///////////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION

#endif
