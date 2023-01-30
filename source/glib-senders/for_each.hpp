#ifndef GLIB_SENDERS_FOR_EACH_HPP
#define GLIB_SENDERS_FOR_EACH_HPP

#include <stdexec/execution.hpp>

namespace gsenders {
namespace for_each_ {
using env_t = stdexec::__make_env_t<
    stdexec::__empty_env,
    stdexec::__with<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>;

template <class... Senders> struct operation_state_base {};

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

template <class... Senders> class receiver;

template <class... Senders> struct shared_state {
  static constexpr std::size_t N = sizeof...(Senders);
  using start_fn = void (*)(operation_state_base<Senders...>*);
  using complete_fn = void (*)(operation_state_base<Senders...>*);

  std::mutex vtable_mutex_;
  std::array<start_fn, N> start_{};
  std::array<complete_fn, N> complete_{};
  std::array<operation_state_base<Senders...>*, N> ops_{};

  std::tuple<Senders...> senders_;
  std::tuple<stdexec::connect_result_t<Senders, receiver<shared_state>>...>
      operation_states_;
  std::atomic<operation_status> state_{operation_status::start};

  std::array<stdexec::in_place_stop_source, N> local_stop_sources_{};
  stdexec::in_place_stop_source global_stop_source_;

  std::array<result_type_t<Senders...>, N> results_{};
  std::atomic<std::size_t> count_{0};
  std::atomic<std::size_t> connections_{0};
};

template <class... Senders> class receiver {
  shared_state<Senders...>* shared_state_;

  template <class CPO, class... Args> void notify(CPO, Args&&... args) {
    auto rank = shared_state_->count_.fetch_add(1, std::memory_order_relaxed);
    assert(rank < sizeof...(Senders));
    if (shared_state_->local_stop_sources_[rank].stop_requested()) {
      state_.results_[rank]
          .template emplace<std::tuple<stdexec::set_stopped_t>>(
              stdexec::set_stopped);
    } else {
      try {
        state_.results_[rank]
            .template emplace<std::tuple<CPO, std::decay_t<Args>>>(
                CPO{}, (Args &&) args...);
      } catch (...) {
        state_.results_[rank]
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
      }
      shared_state_->complete_[rank](shared_state_->ops_[rank]);
    }
  }

  friend auto tag_invoke(stdexec::get_env_t, const receiver& self) noexcept
      -> env_t {
    using with_token = stdexec::__with<stdexec::get_stop_token_t,
                                       stdexec::in_place_stop_token>;
    return env_t{with_token{op_->global_stop_source_.get_token()}};
  }
};

template <class Receiver, class... Senders>
class operation : public operation_state_base {
  using shared_state_ptr = std::shared_ptr<shared_state<Senders...>>;

  shared_state_ptr state_;
  int index_;
  Receiver receiver_;
  operation_status status_{operation_status::start};

  struct on_stop_requested {
    stdexec::in_place_stop_source& stop_source_;
    void operator()() noexcept { stop_source_.request_stop(); }
  };
  using on_stop = std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<Receiver>&>::template callback_type<on_stop_requested>>;
  on_stop on_receiver_stop_{};
  using on_shared_stop = std::optional<
      stdexec::inplace_stop_token::callback_type<on_stop_requested>>;
  on_shared_stop on_shared_stop_{};

  operation(shared_state_ptr&& state, int index, Receiver&& receiver)
      : state_{std::move(state)}, index_{index}, receiver_{(Receiver &&)
                                                               receiver} {
    on_stop_.emplace(std::exec::get_stop_token(receiver_),
                     on_stop_requested{state_->stop_source_});
    on_shared_stop_.emplace(state_->stop_source_.get_token(),
                            on_stop_requested{state_->stop_source_});
    std::scoped_lock lock{state_->vtable_mutex_};
    state_->count_.fetch_add(1, std::memory_order_relaxed);
    state_->complete_[index_] = [](operation_state_base<Senders...>* op) {
      static_cast<operation*>(op)->complete();
    };
    state_->start_[index_] = [](operation_state_base<Senders...>* op) {
      static_cast<operation*>(op)->start();
    };
    state_->ops_[index_] = this;
  }

  auto start() noexcept -> void {
    state_ = operation_state::running;
    operation_state_.start();
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
        (result_type_t<Senders...> &&) state_.results_[index_]);
  }

  friend void tag_invoke(stdexec::start_t, operation& self) {
    operation_status expected = operation_status::start;
    // Check wheter we are responsible to start all operations
    self.state_->state_.compare_exchange_strong(expected,
                                                operation_status::running);
    switch (expected) {
    case operation_status::start: {
      std::scoped_lock lock{state_->vtable_mutex_};
      // We are responsible to start all operations
      for (std::size_t i = 0; i < N; ++i) {
        if (state_->start_[i]) {
          state_->start_[i](state_->ops_[i]);
        }
      }
    } break;
    case operation_status::running: {
      // We are not responsible to start all operations
      // We are responsible to start the current operation
      std::scoped_lock lock{state_->vtable_mutex_};
      if (status_ == operation_status::start) {
        start();
      }
    } break;
    case operation_status::done: {
      // We are not responsible to start all operations
      // We are not responsible to start the current operation
      // We are responsible to complete the current operation
      complete();
    } break;
    }
  }
};

template <class... Senders> class sender {
  using shared_state_ptr = std::shared_ptr<shared_state<Senders...>>;

  shared_state_ptr state_;
  int index_;

  sender(const sender&) = delete;
  sender& operator=(const sender&) = delete;

  sender(sender&&) = default;
  sender& operator=(sender&&) = default;

  template <class Receiver>
  friend auto tag_invoke(stdexec::connect_t, sender&& self,
                         Receiver&& receiver) && {
    if (!self.state_) {
      throw std::logic_error("state is nullptr");
    }
    if (!(0 <= index_ && index_ < sizeof...(Senders))) {
      throw std::logic_error("invalid index");
    }

    return operation<Receiver, Senders...>((shared_state_ptr &&) self.state_,
                                           std::exchange(index_, -1),
                                           (Receiver &&) receiver);
  }
};

struct for_each_t {
  template <typename... Senders>
  auto operator()(Senders&&... senders) const {
    auto state = std::make_shared<shared_state<std::decay_t<Senders>...>>();
    state->senders_ = std::tuple{((Senders&&) senders)...};
    std::array<sender<std::decay_t<Senders>...>, sizeof...(Senders)> senders{};
    for (std::size_t i = 0; i < sizeof...(Senders); ++i) {
      senders_[i].state_ = state;
      senders_[i].index_ = i;
    }
    return senders;
  }
};

inline constexpr for_each_t for_each;

} // namespace for_each_

using for_each_::for_each;

} // namespace gsenders

#endif