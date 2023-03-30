#pragma once

#include "./sequence_sender_concepts.hpp"

#include <stdexec/execution.hpp>

namespace gsenders {
  namespace enumerate_ {
    using namespace stdexec;

    template <std::integral Int, class Receiver>
    struct operation_base {
      [[no_unique_address]] Receiver rcvr_;
      std::atomic<Int> count_{};

      template <__decays_to<Receiver> Rcvr>
      operation_base(Rcvr&& rcvr, Int initial_val) noexcept(
        std::is_nothrow_constructible_v<Receiver, Rcvr>)
        : rcvr_{static_cast<Rcvr&&>(rcvr)}
        , count_{initial_val} {
      }
    };

    template <class Int>
    struct increase_count {
      std::atomic<Int>* count_;

      template <class... Args>
      auto operator()(Args&&... args) const {
        const Int count = count_->fetch_add(1, std::memory_order_relaxed);
        return just(count, static_cast<Args&&>(args)...);
      }
    };

    template <class Int>
    struct next_item_t {
      template <class Item>
      using __f = decltype(let_value(__declval<Item&&>(), __declval<increase_count<Int>>()));
    };

    template <class Sender, class Env, class Int>
    using sequence_items_t =
      __make_sequence_items_t<Sender, Env, sequence_items<>, next_item_t<Int>>;

    template <std::integral Int, class ReceiverId>
    struct receiver {
      using Receiver = stdexec::__t<ReceiverId>;

      struct __t {
        explicit __t(operation_base<Int, Receiver>* op) noexcept
          : op_(op) {
        }

        friend void tag_invoke(set_value_t, __t&& self) noexcept {
          set_value(static_cast<Receiver&&>(self.op_->rcvr_));
        }

        template <class Item>
        friend auto tag_invoke(set_next_t, __t& self, Item&& item) noexcept {
          return set_next(
            self.op_->rcvr_,
            let_value(static_cast<Item&&>(item), increase_count<Int>{&self.op_->count_}));
        }

        template <class Error>
        friend void tag_invoke(set_error_t, __t&& self, Error&& error) noexcept {
          set_error((Receiver&&) self.op_->rcvr_, (Error&&) error);
        }

        friend void tag_invoke(set_stopped_t, __t&& self) noexcept {
          set_value((Receiver&&) self.op_->rcvr_);
        }

        friend env_of_t<Receiver> tag_invoke(get_env_t, const __t& self) noexcept {
          return get_env(self.op_->rcvr_);
        }

        operation_base<Int, Receiver>* op_;
      };
    };
    template <std::integral Int, class Receiver>
    using receiver_t = __t<receiver<Int, __id<Receiver>>>;

    template <class Int, class SenderId, class ReceiverId>
    struct operation {
      using Sender = stdexec::__t<SenderId>;
      using Receiver = stdexec::__t<ReceiverId>;

      struct __t : operation_base<Int, Receiver> {
        template <class Sndr, class Rcvr>
        __t(Sndr&& sndr, Rcvr&& rcvr, Int initial_val)
          : operation_base<Int, Receiver>(static_cast<Rcvr&&>(rcvr), initial_val)
          , op_(connect(static_cast<Sndr&&>(sndr), stdexec::__t<receiver<Int, ReceiverId>>{this})) {
        }

        connect_result_t<Sender, stdexec::__t<receiver<Int, ReceiverId>>> op_;

        friend void tag_invoke(start_t, __t& self) noexcept {
          start(self.op_);
        }
      };
    };
    template <class Int, class Sender, class Receiver>
    using operation_t = __t<operation<Int, __id<Sender>, __id<Receiver>>>;

    template <std::integral Int, class SenderId>
    struct sender {
      using Sender = stdexec::__t<SenderId>;

      struct __t {
        [[no_unique_address]] Sender sndr_;
        Int initial_val_;

        template <__decays_to<Sender> Sndr>
        __t(Sndr&& sndr, Int initial_val = Int{}) noexcept(
          std::is_nothrow_constructible_v<Sender, Sndr>)
          : sndr_{static_cast<Sndr&&>(sndr)}
          , initial_val_{initial_val} {
        }

        template <__decays_to<__t> Self, class Rcvr>
          requires sequence_receiver_of<
                     Rcvr,
                     sequence_items_t<__copy_cvref_t<Self, Sender>, env_of_t<Rcvr>, Int>>
                && sequence_sender_to<__copy_cvref_t<Self, Sender>, receiver_t<Int, decay_t<Rcvr>>>
        friend auto tag_invoke(connect_t, Self&& self, Rcvr&& rcvr)
          -> operation_t<Int, __copy_cvref_t<Self, Sender>, std::decay_t<Rcvr>> {
          return operation_t<Int, Sender, std::decay_t<Rcvr>>(
            static_cast<Self&&>(self).sndr_, static_cast<Rcvr&&>(rcvr), self.initial_val_);
        }

        template <__decays_to<__t> Self, class Env>
        friend auto tag_invoke(get_completion_signatures_t, Self&& self, const Env& env)
          -> completion_signatures_of_t<__copy_cvref_t<Self, Sender>, Env>;

        template <__decays_to<__t> Self, class Env>
        friend auto tag_invoke(get_sequence_items_t, Self&& self, const Env& env)
          -> sequence_items_t<__copy_cvref_t<Self, Sender>, Env, Int>;
      };
    };

    struct enumerate_t {
      template <sequence_sender Sender, std::integral Int = int>
      auto operator()(Sender&& sndr, Int initial_val = Int{}) const noexcept {
        return __t<sender<Int, __id<decay_t<Sender>>>>{static_cast<Sender&&>(sndr), initial_val};
      }

      template <std::integral Int = int>
      auto operator()(Int initial_val = Int{}) const noexcept -> __binder_back<enumerate_t, Int> {
        return {{}, {}, {initial_val}};
      }
    };
  } // namespace enumerate_

  inline constexpr enumerate_::enumerate_t enumerate{};

} // namespace gsenders