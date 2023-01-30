#ifndef GLIB_SENDERS_WHEN_ANY_HPP
#define GLIB_SENDERS_WHEN_ANY_HPP

#include <tuple>

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

  template <stdexec::__one_of<stdexec::set_value_t, stdexec::set_error_t,
                              stdexec::set_stopped_t>
                CPO,
            class... Args>
  friend void tag_invoke(CPO, receiver&& self, Args&&... args) noexcept {
    self.notify(CPO{}, ((Args &&) args)...);
  }

  friend auto tag_invoke(stdexec::get_env_t, const receiver&) noexcept
      -> stdexec::__empty_env {
    return {};
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
                                           stdexec::__q<std::variant>>,
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

  std::atomic<bool> stopped_{false};
  std::atomic<int> count_{sizeof...(Senders)};
  Receiver receiver_;

  result_type_t<Senders...> result_;

  template <class CPO, class... Args>
  void notify(CPO, Args&&... args) noexcept {
    if (!stopped_.exchange(true)) {
      result_.template emplace<std::tuple<CPO, std::decay_t<Args>...>>(
          CPO{}, ((Args &&) args)...);
    }
    if (count_.fetch_sub(1) == 1) {
      std::visit(
          [this]<class Tuple>(Tuple&& result) {
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
          },
          (result_type_t<Senders...> &&) result_);
    }
  }

  std::tuple<stdexec::connect_result_t<
      Senders, receiver<Senders, Receiver, operation>>...>
      operation_states_;

  friend void tag_invoke(stdexec::start_t, operation& self) noexcept {
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