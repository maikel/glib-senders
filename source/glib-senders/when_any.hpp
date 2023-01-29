#ifndef GLIB_SENDERS_WHEN_ANY_HPP
#define GLIB_SENDERS_WHEN_ANY_HPP

#include <tuple>

#include <stdexec/execution.hpp>

namespace gsenders {

template <typename BaseEnv>
using when_any_env_t = stdexec::__make_env_t<
    BaseEnv,
    stdexec::__with<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>;

template <class Sender, class Receiver, class Op> struct when_any_receiver {
  Op* op_;

  template <class... Args>
  friend void tag_invoke(stdexec::set_value_t, when_any_receiver&& self,
                         Args&&... args) noexcept {
    if (!self.op_->stopped_.exchange(true)) {
      stdexec::set_value((Receiver &&) self.op_->receiver_,
                         ((Args &&) args)...);
    }
  }

  template <class Error>
  friend void tag_invoke(stdexec::set_error_t, when_any_receiver&& self,
                         Error&& error) noexcept {
    if (!self.op_->stopped_.exchange(true)) {
      stdexec::set_error((Receiver &&) self.op_->receiver_, (Error &&) error);
    }
  }

  friend void tag_invoke(stdexec::set_stopped_t,
                         when_any_receiver&& self) noexcept {
    if (!self.op_->stopped_.exchange(true)) {
      stdexec::set_stopped((Receiver &&) self.op_->receiver_);
    }
  }

  friend auto tag_invoke(stdexec::get_env_t, const when_any_receiver&) noexcept
      -> stdexec::__empty_env {
    return {};
  }
};

template <class Receiver, class... Senders> class when_any_operation {
public:
  template <class SenderTuple>
  when_any_operation(SenderTuple&& tuple, Receiver&& receiver)
      : when_any_operation((SenderTuple &&) tuple, (Receiver &&) receiver,
                           std::make_index_sequence<sizeof...(Senders)>()) {}

  template <class SendersTuple, std::size_t... Is>
  when_any_operation(SendersTuple&& senders, Receiver&& receiver,
                     std::index_sequence<Is...>)
      : receiver_{(Receiver &&) receiver},
        operation_states_{stdexec::__conv{[&senders, this]() {
          return stdexec::connect(
              std::get<Is>((SendersTuple &&) senders),
              when_any_receiver<std::tuple_element_t<Is, SendersTuple>,
                                Receiver, when_any_operation>{this});
        }}...} {}

public:
  std::atomic<bool> stopped_{false};
  Receiver receiver_;

  std::tuple<stdexec::connect_result_t<
      Senders, when_any_receiver<Senders, Receiver, when_any_operation>>...>
      operation_states_;

  friend void tag_invoke(stdexec::start_t, when_any_operation& self) noexcept {
    std::apply([](auto&... ops) { (stdexec::start(ops), ...); },
               self.operation_states_);
  }
};

template <class Sender, class... Senders> struct when_any_sender {
  using AddSigl = stdexec::__minvoke<
      stdexec::__mconcat<stdexec::__q<stdexec::completion_signatures>>,
      stdexec::completion_signatures_of_t<Senders>...>;

  using completion_signatures =
      stdexec::make_completion_signatures<Sender, stdexec::no_env, AddSigl>;

  static_assert(stdexec::__valid_completion_signatures<completion_signatures,
                                                       stdexec::no_env>);

  std::tuple<Sender, Senders...> senders_;

  template <class Self, class R>
  friend auto tag_invoke(stdexec::connect_t, Self&& self,
                         R&& receiver) noexcept {
    using Receiver = std::remove_cvref_t<R>;
    return when_any_operation<Receiver, Sender, Senders...>(
        (std::tuple<Sender, Senders...> &&) self.senders_, (R &&) receiver);
  }

  friend auto tag_invoke(stdexec::get_attrs_t,
                         const when_any_sender& self) noexcept
      -> stdexec::__empty_attrs {
    return {};
  }
};

struct when_any_t {
  template <class... Senders>
  requires(stdexec::sender<Senders> && ...)
  auto operator()(Senders&&... senders) const noexcept(
      std::conjunction_v<
          std::is_nothrow_constructible<std::decay_t<Senders>, Senders>...>) {
    static_assert(stdexec::sender<when_any_sender<Senders...>>);
    return when_any_sender<std::decay_t<Senders>...>{
        std::tuple<std::decay_t<Senders>...>{((Senders &&) senders)...}};
  }
};

inline constexpr when_any_t when_any{};

} // namespace gsenders

#endif