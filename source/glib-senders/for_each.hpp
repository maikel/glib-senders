#ifndef GLIB_SENDERS_FOR_EACH_HPP
#define GLIB_SENDERS_FOR_EACH_HPP

#include <stdexec/execution.hpp>

namespace gsenders {
namespace for_each_ {
using env_t = stdexec::__make_env_t<
    stdexec::__empty_env,
    stdexec::__with<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>;

enum class operation_status { start, running, done };

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

template <class... Senders> struct shared_state;

template <class... Senders> struct receiver {
  shared_state<Senders...>* shared_state_;

  template <class CPO, class... Args>
  void notify(CPO, Args&&... args) noexcept {
    auto rank = shared_state_->count_.fetch_add(1, std::memory_order_relaxed);
    assert(rank < sizeof...(Senders));
    if (shared_state_->local_stop_sources_[rank].stop_requested()) {
      shared_state_->results_[rank]
          .template emplace<std::tuple<stdexec::set_stopped_t>>(
              stdexec::set_stopped);
    } else {
      try {
        shared_state_->results_[rank]
            .template emplace<std::tuple<CPO, std::decay_t<Args>...>>(
                CPO{}, ((Args &&) args)...);
      } catch (...) {
        shared_state_->results_[rank]
            .template emplace<
                std::tuple<stdexec::set_error_t, std::exception_ptr>>(
                stdexec::set_error, std::current_exception());
      }
    }
    if (shared_state_->complete_[rank]) {
      auto count =
          shared_state_->connections_.fetch_sub(1, std::memory_order_relaxed);
      if (count == 1) {
        shared_state_->global_stop_source_.request_stop();
        shared_state_->state_.store(operation_status::done,
                                    std::memory_order_relaxed);
      }
      shared_state_->complete_[rank](shared_state_->ops_[rank]);
    }
  }

  template <stdexec::__one_of<stdexec::set_value_t, stdexec::set_stopped_t,
                              stdexec::set_error_t>
                CPO,
            class... Args>
  friend void tag_invoke(CPO cpo, receiver&& self, Args&&... args) noexcept {
    self.notify(cpo, (Args &&) args...);
  }

  friend auto tag_invoke(stdexec::get_env_t, const receiver& self) noexcept
      -> env_t {
    using with_token = stdexec::__with<stdexec::get_stop_token_t,
                                       stdexec::in_place_stop_token>;
    return env_t{
        with_token{self.shared_state_->global_stop_source_.get_token()}};
  }
};

template <class... Senders> struct shared_state {
  static constexpr std::size_t N = sizeof...(Senders);
  using complete_fn = void (*)(void*);

  shared_state(Senders&&... senders)
      : operation_states_{stdexec::__conv{[&, this] {
          return stdexec::connect((Senders &&) senders,
                                  receiver<Senders...>{this});
        }}...} {}

  std::tuple<stdexec::connect_result_t<Senders, receiver<Senders...>>...>
      operation_states_;

  std::array<complete_fn, N> complete_{};
  std::array<void*, N> ops_{};

  std::atomic<operation_status> state_{operation_status::start};

  std::array<stdexec::in_place_stop_source, N> local_stop_sources_{};
  stdexec::in_place_stop_source global_stop_source_;

  std::array<result_type_t<Senders...>, N> results_{};
  std::atomic<std::size_t> count_{0};
  std::atomic<std::size_t> connections_{0};
};

template <class Receiver, class... Senders> struct operation {
  using shared_state_ptr = std::shared_ptr<shared_state<Senders...>>;

  shared_state_ptr state_;
  int index_;
  Receiver receiver_;

  struct on_stop_requested {
    stdexec::in_place_stop_source& stop_source_;
    void operator()() noexcept { stop_source_.request_stop(); }
  };
  using on_stop = std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<Receiver>&>::template callback_type<on_stop_requested>>;
  on_stop on_receiver_stop_{};
  using on_shared_stop = std::optional<
      stdexec::in_place_stop_token::callback_type<on_stop_requested>>;
  on_shared_stop on_shared_stop_{};

  operation(shared_state_ptr&& state, int index, Receiver&& receiver)
      : state_{std::move(state)}, index_{index}, receiver_{(Receiver &&)
                                                               receiver} {
    on_receiver_stop_.emplace(
        stdexec::get_stop_token(stdexec::get_env(receiver_)),
        on_stop_requested{state_->local_stop_sources_[index_]});
    on_shared_stop_.emplace(
        state_->global_stop_source_.get_token(),
        on_stop_requested{state_->local_stop_sources_[index_]});

    state_->connections_.fetch_add(1, std::memory_order_relaxed);
    state_->complete_[index_] = [](void* op) {
      static_cast<operation*>(op)->complete();
    };
    state_->ops_[index_] = this;
  }

  auto complete() noexcept -> void {
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
        (result_type_t<Senders...> &&) state_->results_[index_]);
  }

  void start() noexcept {
    operation_status expected = operation_status::start;
    // Check wheter we are responsible to start all operations
    state_->state_.compare_exchange_strong(expected, operation_status::running);
    switch (expected) {
    case operation_status::start: {
      // We are responsible to start all operations
      std::apply([](auto&... o) { (stdexec::start(o), ...); }, state_->operation_states_);
    } break;
    case operation_status::running: {
      // We are not responsible to start any operation
    } break;
    case operation_status::done: {
      // We are not responsible to start all operations
      // We are responsible to complete the current operation
      complete();
    } break;
    }
  }

  friend void tag_invoke(stdexec::start_t, operation& self) noexcept {
    self.start();
  }
};

template <class... Senders> class sender {
public:
  using completion_signatures = completion_signatures_t<Senders...>;

  sender(const sender&) = delete;
  sender& operator=(const sender&) = delete;

  sender(sender&&) = default;
  sender& operator=(sender&&) = default;

private:
  using shared_state_ptr = std::shared_ptr<shared_state<Senders...>>;

  friend struct for_each_t;

  shared_state_ptr state_{nullptr};
  int index_{-1};

  sender() = default;

  template <class Receiver>
  friend auto tag_invoke(stdexec::connect_t, sender&& self,
                         Receiver&& receiver) {
    if (!self.state_) {
      throw std::logic_error("state is nullptr");
    }
    if (!(0 <= self.index_ && self.index_ < sizeof...(Senders))) {
      throw std::logic_error("invalid index");
    }

    return operation<Receiver, Senders...>((shared_state_ptr &&) self.state_,
                                           std::exchange(self.index_, -1),
                                           (Receiver &&) receiver);
  }

  friend auto tag_invoke(stdexec::get_attrs_t, const sender& self) noexcept
      -> stdexec::__empty_attrs {
    return {};
  }
};

struct for_each_t {
  template <typename... Senders> auto operator()(Senders&&... senders) const {
    auto state = std::make_shared<shared_state<std::decay_t<Senders>...>>(
        ((Senders &&) senders)...);
    std::array<sender<std::decay_t<Senders>...>, sizeof...(Senders)> result{};
    static_assert(stdexec::sender<sender<std::decay_t<Senders>...>,
                                  stdexec::__env::no_env>);
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i].state_ = state;
      result[i].index_ = i;
    }
    return result;
  }
};

inline constexpr for_each_t for_each;

} // namespace for_each_

using for_each_::for_each;

} // namespace gsenders

#endif