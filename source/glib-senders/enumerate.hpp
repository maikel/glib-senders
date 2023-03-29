#pragma once

#include "./sequence_sender_concepts.hpp"

#include <stdexec/execution.hpp>

namespace gsenders {
  namespace enumerate_ {

    template <std::integral Int, class Receiver>
    struct operation_base {
      [[no_unique_address]] Receiver rcvr_;
      std::atomic<Int> count_{};

      template <stdexec::__decays_to<Receiver> Rcvr>
      operation_base(Rcvr&& rcvr, Int initial_val) noexcept(
        std::is_nothrow_constructible_v<Receiver, Rcvr>)
        : rcvr_{static_cast<Rcvr&&>(rcvr)}
        , count_{initial_val} {
      }
    };

    template <std::integral Int, class Receiver>
    class receiver {
     public:
      explicit receiver(operation_base<Int, Receiver>& op) noexcept
        : op_(op) {
      }

     private:
      template <class... Args>
      friend void tag_invoke(stdexec::set_value_t, receiver&& self, Args&&... args) noexcept {
        stdexec::set_value(
          static_cast<Receiver&&>(self.op_.rcvr_), self.op_.count_, static_cast<Args&&>(args)...);
      }

      template <stdexec::sender Item>
      friend auto tag_invoke(set_next_t, receiver& self, Item&& item) noexcept {
        return set_next(self.op_.rcvr_, stdexec::let_value(
          static_cast<Item&&>(item), [&self]<class... Args>(Args&&... args) {
            const Int count = self.op_.count_.fetch_add(1, std::memory_order_relaxed);
            return stdexec::just(count, static_cast<Args&&>(args)...);
          }));
      }

      template <class Error>
      friend void tag_invoke(stdexec::set_error_t, receiver&& self, Error&& error) noexcept {
        stdexec::set_error((Receiver&&) self.op_.rcvr_, (Error&&) error);
      }

      friend void tag_invoke(stdexec::set_stopped_t, receiver&& self) noexcept {
        stdexec::set_value((Receiver&&) self.op_.rcvr_, self.op_.count_);
      }

      operation_base<Int, Receiver>& op_;
    };

    template <std::integral Int, class Sender, class Receiver>
    struct operation : operation_base<Int, Receiver> {
      stdexec::connect_result_t<Sender, receiver<Int, Receiver>> op_;

      template <stdexec::__decays_to<Sender> Sndr, stdexec::__decays_to<Receiver> Rcvr>
      operation(Sndr&& sndr, Rcvr&& rcvr, Int initial_val)
        : operation_base<Int, Receiver>(static_cast<Rcvr&&>(rcvr), initial_val)
        , op_(stdexec::connect(sndr, receiver{*this})) {
      }

     private:
      friend void tag_invoke(stdexec::start_t, operation& self) noexcept {
        stdexec::start(self.op_);
      }
    };

    template <std::integral Int, class Sender>
    struct sender {
      [[no_unique_address]] Sender sndr_;
      Int initial_val_;

      template <stdexec::__decays_to<Sender> Sndr>
      sender(Sndr&& sndr, Int initial_val = Int{}) noexcept(
        std::is_nothrow_constructible_v<Sender, Sndr>)
        : sndr_{static_cast<Sndr&&>(sndr)}
        , initial_val_{initial_val} {
      }

      template <stdexec::receiver Rcvr>
      friend auto tag_invoke(stdexec::connect_t, sender&& self, Rcvr&& rcvr) {
        return operation<Int, Sender, std::decay_t<Rcvr>>{
          static_cast<Sender&&>(self.sndr_), static_cast<Rcvr&&>(rcvr), self.initial_val_};
      }
    };

    struct enumerate_t {
      template <std::integral Int, class Sender>
        requires stdexec::sender<Sender>
      auto operator()(Sender&& sndr, Int initial_val = Int{}) const noexcept {
        return sender<Int, std::decay_t<Sender>>{static_cast<Sender&&>(sndr), initial_val};
      }
    };
  } // namespace enumerate_

  inline constexpr enumerate_::enumerate_t enumerate{};

} // namespace gsenders