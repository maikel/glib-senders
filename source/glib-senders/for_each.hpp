#ifndef GLIB_SENDERS_FOR_EACH_HPP
#define GLIB_SENDERS_FOR_EACH_HPP

namespace gsenders {
namespace for_each_ {
template <class... Senders> struct operation_state_base {};

enum class operation_status { start, running, done };

template <class... Senders> struct shared_state {
  static constexpr std::size_t N = sizeof...(Senders);
  using complete_fn = void (*)(operation_state_base<Senders...>*);

  std::tuple<Senders...> senders_;
  std::atomic<operation_status> state_{operation_state::start};

  stdexec::in_place_stop_source stop_source_;

  std::atomic<std::size_t> count_{0};
  std::array<result_type_t<Senders...>, N> results_{};

  std::mutex vtable_mutex_;
  std::array<start_fn, N> start_{};
  std::array<operation_state_base<Senders...>*, N> ops_{};
};

template <class Receiver, class Op> class receiver {
  Op* op_;

  template <class CPO, class... Args> void notify(CPO, Args&&... args) {
    op_->notify(CPO{}, (Args &&) args);
  }

  friend auto tag_invoke(stdexec::get_env_t, const receiver& self) {}
};

template <class Sender, class Receiver, class... Senders>
class operation : public operation_state_base {
  using shared_state_ptr = std::shared_ptr<shared_state<Senders...>>;

  shared_state_ptr state_;
  int index_;
  Receiver receiver_;
  operation_status status_{operation_status::start};
  stdexec::connect_result_t<Sender, receiver<Receiver, operation>>
      operation_state_;

  operation(shared_state_ptr&& state, int index, Receiver&& receiver)
      : state_{std::move(state)}, index_{index}, receiver_{(Receiver &&)
                                                               receiver} {
    std::scoped_lock lock{state_->vtable_mutex_};
    state_->count_.fetch_add(1, std::memory_order_relaxed);
    state_->complete_[index_] = [](operation_state_base<Senders...>* op) {
      static_cast<operation*>(op)->complete();
    };
    state_->complete_[index_] = [](operation_state_base<Senders...>* op) {
      static_cast<operation*>(op)->start();
    };
    state_->ops_[index_] = this;
  }

  auto start() noexcept -> void { 
    state_ = operation_state::running;
    operation_state_.start();
  }

  template <typename CPO, typename... Args>
  auto notify(CPO, Args&&... args) noexcept -> void {
    try {
      state_.results_[index_]
        .template emplace<std::tuple<CPO, std::decay_t<Args>>>(
            CPO{}, (Args &&) args...);
    } catch (...) {
      state_.results_[index_].template emplace<std::exception_ptr>(
          std::current_exception());
    }
    if (state_.count_.fetch_sub(1, std::memory_order_relaxed) == 1) {
      state_.stop_source_.request_stop();
    }
    complete();
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
    return stdexec::connect((shared_state_ptr &&) self.state_,
                            std::exchange(index_, -1), (Receiver &&) receiver);
  }
};

} // namespace for_each_
} // namespace gsenders

#endif