#pragma once

#include "./sequence_sender_concepts.hpp"

#include <stdexec/execution.hpp>

namespace gsenders {
  namespace let_value_each_ {
    using namespace stdexec;

    template <class Receiver, class Fun>
    struct operation_base {
      [[no_unique_address]] Receiver rcvr_;
      [[no_unique_address]] Fun fun_;
    };

    template <class Fun>
    struct apply_fun {
      Fun* fun_;

      template <class... Args>
      auto operator()(Args&&... args) const {
        return (*fun_)(static_cast<Args&&>(args)...);
      }
    };

    template <class Fun>
    struct next_item_t {
      template <class Item>
      using __f = decltype(let_value(__declval<Item>(), __declval<apply_fun<Fun>>()));
    };

    template <class Sender, class Env, class Fun>
    using sequence_items_t =
      __make_sequence_items_t<Sender, Env, sequence_items<>, next_item_t<Fun>>;

    template <class ReceiverId, class Fun>
    struct receiver {
      using Receiver = stdexec::__t<ReceiverId>;

      struct __t {
        operation_base<Receiver, Fun>* op_;

        template <class Item>
        friend auto tag_invoke(set_next_t, __t& self, Item&& item) noexcept {
          return set_next(
            self.op_->rcvr_, let_value(static_cast<Item&&>(item), apply_fun<Fun>{&self.op_->fun_}));
        }

        template <__completion_tag Tag, class... Args>
        friend void tag_invoke(Tag complete, __t&& self, Args&&... args) noexcept {
          complete(static_cast<Receiver&&>(self.op_->rcvr_), static_cast<Args&&>(args)...);
        }

        friend env_of_t<Receiver> tag_invoke(get_env_t, const __t& self) noexcept {
          return get_env(self.op_->rcvr_);
        }
      };
    };
    template <class Receiver, class Fun>
    using receiver_t = __t<receiver<__id<decay_t<Receiver>>, Fun>>;

    template <class SenderId, class ReceiverId, class Fun>
    struct operation {
      using Sender = stdexec::__t<SenderId>;
      using Receiver = stdexec::__t<ReceiverId>;

      struct __t : operation_base<Receiver, Fun> {
        connect_result_t<Sender, receiver_t<Receiver, Fun>> op_;

        template <class Sndr, __decays_to<Receiver> Rcvr>
        __t(Sndr&& sndr, Rcvr&& rcvr, Fun fun)
          : operation_base<Receiver, Fun>(static_cast<Rcvr&&>(rcvr), static_cast<Fun&&>(fun))
          , op_(connect(static_cast<Sndr&&>(sndr), receiver_t<Receiver, Fun>{this})) {
        }

       private:
        friend void tag_invoke(start_t, __t& self) noexcept {
          start(self.op_);
        }
      };
    };
    template <class Sender, class Receiver, class Fun>
    using operation_t = __t<operation<__id<Sender>, __id<decay_t<Receiver>>, Fun>>;

    template <class Sender, class Env, class Fun>
    using compl_sigs = make_completion_signatures< Sender, Env>;

    template <class Sender, class Fun>
    struct sender {
      struct __t {
        [[no_unique_address]] Sender sndr_;
        [[no_unique_address]] Fun fun_;

        template <__decays_to<Sender> Sndr>
        __t(Sndr&& sndr, Fun fun)
          : sndr_{static_cast<Sndr&&>(sndr)}
          , fun_{static_cast<Fun&&>(fun)} {
        }

        template <__decays_to<__t> Self, stdexec::receiver Rcvr>
          requires sequence_sender_to<__copy_cvref_t<Self, Sender>, Rcvr>
        friend operation_t<__copy_cvref_t<Self, Sender>, Rcvr, Fun>
          tag_invoke(connect_t, Self&& self, Rcvr&& rcvr) {
          return operation_t<__copy_cvref_t<Self, Sender>, Rcvr, Fun>(
            static_cast<Self&&>(self).sndr_,
            static_cast<Rcvr&&>(rcvr),
            static_cast<Self&&>(self).fun_);
        }

        template <class Env>
        friend auto tag_invoke(get_completion_signatures_t, const __t& self, const Env& env)
          -> compl_sigs<Sender, Env, Fun>;

        template <class Env>
        friend auto tag_invoke(get_sequence_items_t, const __t& self, const Env& env) 
          -> sequence_items_t<Sender, Env, Fun>;
      };
    };

    struct let_value_each_t {
      template <stdexec::sender Sender, class Fun>
      auto operator()(Sender&& sndr, Fun fun) const {
        return __t<sender<decay_t<Sender>, Fun>>{
          static_cast<Sender&&>(sndr), static_cast<Fun&&>(fun)};
      }

      template <class Fun>
      constexpr auto operator()(Fun fun) const noexcept -> __binder_back<let_value_each_t, Fun> {
        return {{}, {}, {static_cast<Fun&&>(fun)}};
      }
    };
  } // namespace let_value_each_

  inline constexpr let_value_each_::let_value_each_t let_value_each{};
}