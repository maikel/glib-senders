#ifndef GLIB_SENDERS_WHEN_ANY_HPP
#define GLIB_SENDERS_WHEN_ANY_HPP

#include <atomic>
#include <concepts>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <stdexec/execution.hpp>

namespace gsenders {
namespace when_any_ {
template <class BaseEnv>
using env_t = stdexec::__make_env_t<
    BaseEnv,
    stdexec::__with<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>;

template <class Sender, class Receiver, class Op> class receiver {
public:
  explicit receiver(Op* op) noexcept : op_{op} {}

private:
  Op* op_;

  template <class CPO, class... Args>
  void notify(CPO, Args&&... args) noexcept {
    op_->notify(CPO{}, ((Args &&) args)...);
  }

  auto get_env() const noexcept -> env_t<stdexec::env_of_t<Receiver>> {
    using with_token = stdexec::__with<stdexec::get_stop_token_t,
                                       stdexec::in_place_stop_token>;
    auto token = with_token{op_->stop_source_.get_token()};
    auto env = stdexec::get_env(op_->receiver_);
    return env_t<stdexec::env_of_t<Receiver>>((with_token &&) token, env);
  }

  template <stdexec::__one_of<stdexec::set_value_t, stdexec::set_error_t,
                              stdexec::set_stopped_t>
                CPO,
            class... Args>
  friend void tag_invoke(CPO, receiver&& self, Args&&... args) noexcept {
    self.notify(CPO{}, ((Args &&) args)...);
  }

  friend auto tag_invoke(stdexec::get_env_t, const receiver& self) noexcept
      -> env_t<stdexec::env_of_t<Receiver>> {
    return self.get_env();
  }
};

template <class... Senders> struct completion_signatures;

template <class Sender, class... Senders>
struct completion_signatures<Sender, Senders...> {
  using AddSigl = stdexec::__minvoke<
      stdexec::__mconcat<stdexec::__q<stdexec::completion_signatures>>,
      stdexec::completion_signatures_of_t<Senders>...>;

  using type =
      stdexec::make_completion_signatures<Sender, stdexec::no_env, AddSigl>;
};

template <class... Senders>
using completion_signatures_t =
    typename completion_signatures<Senders...>::type;

template <class Sig> struct signature_to_tuple;

template <class Ret, class... Args> struct signature_to_tuple<Ret(Args...)> {
  using type = std::tuple<Ret, Args...>;
};

template <class Sig>
using signature_to_tuple_t = typename signature_to_tuple<Sig>::type;

template <class... Senders>
using result_type_t =
    stdexec::__mapply<stdexec::__transform<stdexec::__q<signature_to_tuple_t>,
                                           stdexec::__mbind_front_q<
                                               std::variant, std::monostate>>,
                      completion_signatures_t<Senders...>>;

template <class Receiver, class... Senders> class operation {
public:
  operation(operation&&) = delete;

  template <class SenderTuple>
  operation(SenderTuple&& tuple, Receiver&& receiver)
      : operation((SenderTuple &&) tuple, (Receiver &&) receiver,
                  std::make_index_sequence<sizeof...(Senders)>()) {}

private:
  template <class, class, class> friend class receiver;

  template <class SendersTuple, std::size_t... Is>
  operation(SendersTuple&& senders, Receiver&& rcvr, std::index_sequence<Is...>)
      : receiver_{(Receiver &&) rcvr},
        operation_states_{stdexec::__conv{[&senders, this]() {
          return stdexec::connect(
              std::get<Is>((SendersTuple &&) senders),
              receiver<std::tuple_element_t<Is, SendersTuple>, Receiver,
                       operation>{this});
        }}...} {}

  struct on_stop_requested {
    stdexec::in_place_stop_source& stop_source_;
    void operator()() noexcept { stop_source_.request_stop(); }
  };

  std::atomic<int> count_{sizeof...(Senders)};
  stdexec::in_place_stop_source stop_source_;
  using on_stop = std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<Receiver>&>::template callback_type<on_stop_requested>>;
  on_stop on_stop_{};
  Receiver receiver_;

  result_type_t<Senders...> result_;

  template <class CPO, class... Args>
  void notify(CPO, Args&&... args) noexcept {
    if (!stop_source_.request_stop()) {
      result_.template emplace<std::tuple<CPO, std::decay_t<Args>...>>(
          CPO{}, ((Args &&) args)...);
    }
    if (count_.fetch_sub(1) == 1) {
      std::visit(
          [this]<class Tuple>(Tuple&& result) {
            if constexpr (std::same_as<std::decay_t<Tuple>, std::monostate>) {
              stdexec::set_stopped((Receiver &&) receiver_);
            } else {
              std::apply(
                  [this]<class C, class... As>(C, As&&... args) noexcept {
                    try {
                      stdexec::tag_invoke(C{}, (Receiver &&) receiver_,
                                          (As &&) args...);
                    } catch (...) {
                      stdexec::set_error((Receiver &&) receiver_,
                                         std::current_exception());
                    }
                  },
                  (Tuple &&) result);
            }
          },
          (result_type_t<Senders...> &&) result_);
    }
  }

  std::tuple<stdexec::connect_result_t<
      Senders, receiver<Senders, Receiver, operation>>...>
      operation_states_;

  friend void tag_invoke(stdexec::start_t, operation& self) noexcept {
    self.on_stop_.emplace(
        stdexec::get_stop_token(stdexec::get_env(self.receiver_)),
        on_stop_requested{self.stop_source_});
    std::apply([](auto&... ops) { (stdexec::start(ops), ...); },
               self.operation_states_);
  }
};

template <class Sender, class... Senders> struct sender {
  using completion_signatures = completion_signatures_t<Sender, Senders...>;

  static_assert(stdexec::__valid_completion_signatures<completion_signatures,
                                                       stdexec::no_env>);

  std::tuple<Sender, Senders...> senders_;

  template <class Self, class R>
  friend auto tag_invoke(stdexec::connect_t, Self&& self,
                         R&& receiver) noexcept {
    using Receiver = std::remove_cvref_t<R>;
    return operation<Receiver, Sender, Senders...>(
        (std::tuple<Sender, Senders...> &&) self.senders_, (R &&) receiver);
  }

  friend auto tag_invoke(stdexec::get_attrs_t, const sender& self) noexcept
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
    static_assert(stdexec::sender<sender<Senders...>>);
    return sender<std::decay_t<Senders>...>{
        std::tuple<std::decay_t<Senders>...>{((Senders &&) senders)...}};
  }
};

inline constexpr when_any_t when_any{};
} // namespace when_any_
using when_any_::when_any;
} // namespace gsenders

#endif