#pragma

#include <stdexec/execution.hpp>

namespace exec {
namespace ex = stdexec;
struct completes_if {
  using completion_signatures = ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>;

  bool condition_;

  template <class Receiver>
  struct operation {

    bool condition_;
    Receiver rcvr_;

    // without this synchronization, the thread sanitzier shows a race for construction and destruction of on_stop_
    enum class state_t { construction, emplaced, stopped };
    std::atomic<state_t> state_{state_t::construction};

    struct on_stopped {
      operation& self_;
      void operator()() noexcept {
        state_t expected = self_.state_.load(std::memory_order_relaxed);
        while (!self_.state_.compare_exchange_weak(expected, state_t::stopped, std::memory_order_acq_rel));
        if (expected == state_t::emplaced) {
          ex::set_stopped(std::move(self_.rcvr_)); 
        }
      }
    };

    using callback_t = typename ex::stop_token_of_t<ex::env_of_t<Receiver>&>::template callback_type<on_stopped>;
    std::optional<callback_t> on_stop_{};

    friend void tag_invoke(ex::start_t, operation& self) noexcept {
      if (self.condition_) {
        ex::set_value(std::move(self.rcvr_));
      } else {
        self.on_stop_.emplace(ex::get_stop_token(ex::get_env(self.rcvr_)), on_stopped{self});
        state_t expected = state_t::construction;
        if (!self.state_.compare_exchange_strong(expected, state_t::emplaced, std::memory_order_acq_rel)) {
          ex::set_stopped(std::move(self.rcvr_));
        }
      }
    }
  };

  template <ex::__decays_to<completes_if> Self, class Receiver>
  friend operation<std::decay_t<Receiver>> tag_invoke(ex::connect_t, Self&& self, Receiver&& rcvr) noexcept {
    return {self.condition_, std::forward<Receiver>(rcvr)};
  }

  friend ex::__empty_env tag_invoke(ex::get_env_t, const completes_if&) noexcept { return {}; }
};

}