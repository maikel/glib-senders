#pragma once

#include "./sequence_sender_concepts.hpp"

#include <stdexec/execution.hpp>

#include <exec/on.hpp>
#include <exec/trampoline_scheduler.hpp>

namespace gsenders {
  namespace repeat_effect_ {
    using namespace stdexec;

    template <class ReceiverId>
    struct operation_base {
      [[no_unique_address]] __t<ReceiverId> rcvr_;
      void (*repeat_)(operation_base*) noexcept = nullptr;
    };

    template <class ReceiverId>
    struct repeat_receiver {
      using Receiver = stdexec::__t<ReceiverId>;

      struct __t {
        operation_base<ReceiverId>* op_;

        template <class... Args>
        friend void tag_invoke(set_value_t, __t&& self, Args&&...) noexcept {
          static_assert(sizeof...(Args) == 0);
          self.op_->repeat_(self.op_);
        }

        template <__decays_to<__t> Self, class Error>
          requires __callable<set_error_t, Receiver&&, Error&&>
        friend void tag_invoke(set_error_t, Self&& self, Error e) noexcept {
          set_error(static_cast<Receiver&&>(self.op_->rcvr_), static_cast<Error&&>(e));
        }

        friend void tag_invoke(set_stopped_t, __t&& self) noexcept {
          auto token = get_stop_token(get_env(self.op_->rcvr_));
          if (token.stop_requested()) {
            set_stopped(static_cast<Receiver&&>(self.op_->rcvr_));
          } else {
            set_value(static_cast<Receiver&&>(self.op_->rcvr_));
          }
        }

        friend env_of_t<Receiver> tag_invoke(get_env_t, const __t& self) noexcept(
          __nothrow_callable<get_env_t, const Receiver&>) {
          return get_env(self.op_->rcvr_);
        }
      };
    };
    template <class ReceiverId>
    using repeat_receiver_t = __t<repeat_receiver<ReceiverId>>;

    // Takes a sender and creates a sequence sender by repeating the sender as item to the set_next
    // operation.
    template <class SourceSenderId, class ReceiverId>
    struct operation {
      using SourceSender = stdexec::__t<SourceSenderId>;
      using Receiver = stdexec::__t<ReceiverId>;

      using NextSender = __call_result_t<set_next_t, Receiver&, SourceSender>;

      struct __t : operation_base<ReceiverId> {
        SourceSender source_;
        exec::trampoline_scheduler trampoline_;
        using next_on_scheduler_sender =
          __call_result_t<on_t, exec::trampoline_scheduler, NextSender&&>;

        std::optional<connect_result_t<next_on_scheduler_sender, repeat_receiver_t<ReceiverId>>>
          next_op_;

        void repeat() noexcept {
          auto token = get_stop_token(get_env(this->rcvr_));
          if (token.stop_requested()) {
            set_stopped(static_cast<Receiver&&>(this->rcvr_));
            return;
          }
          try {
            auto& next = next_op_.emplace(__conv{[&] {
              return connect(
                stdexec::on(trampoline_, set_next(this->rcvr_, SourceSender{source_})),
                repeat_receiver_t<ReceiverId>{this});
            }});
            start(next);
          } catch (...) {
            set_error(static_cast<Receiver&&>(this->rcvr_), std::current_exception());
          }
        }

        friend void tag_invoke(start_t, __t& self) noexcept {
          self.repeat();
        }

       public:
        template <__decays_to<SourceSender> Sndr, __decays_to<Receiver> Rcvr>
        explicit __t(Sndr&& source, Rcvr&& rcvr)
          : operation_base<ReceiverId>{static_cast<Rcvr&&>(rcvr), 
            [](operation_base<ReceiverId>* self) noexcept { 
              static_cast<__t*>(self)->repeat(); 
            }}
          , source_(static_cast<Sndr&&>(source)) {
        }
      };
    };

    template <class SourceSender, class Receiver>
    using operation_t = __t<operation<__id<SourceSender>, __id<decay_t<Receiver>>>>;

    template <class Source, class Env>
    using compl_sigs = make_completion_signatures<
      Source,
      Env,
      completion_signatures<set_error_t(std::exception_ptr), set_stopped_t()>>;

    template <class SourceId>
    struct sender {
      using Source = stdexec::__t<decay_t<SourceId>>;

      template <class Rcvr>
      using next_sender = __call_result_t<set_next_t, decay_t<Rcvr>&, Source>;

      template <class Rcvr>
      using next_on_scheduler_sender =
        __call_result_t<on_t, exec::trampoline_scheduler, next_sender<Rcvr>&&>;

      template <class Rcvr>
      using repeat_receiver_type = repeat_receiver_t<__id<decay_t<Rcvr>>>;

      class __t {
        Source source_;

        using sequence_items_t = sequence_items<Source>;

        template <__decays_to<__t> Self, class Receiver>
          requires sequence_receiver_of<Receiver, sequence_items_t>
                && sender_to<next_on_scheduler_sender<Receiver>, repeat_receiver_type<Receiver>>
        friend operation_t<Source, Receiver> tag_invoke(connect_t, Self&& self, Receiver&& rcvr) {
          return operation_t<Source, Receiver>{
            static_cast<Self&&>(self).source_, static_cast<Receiver&&>(rcvr)};
        }

        template <__decays_to<__t> Self, class Env>
        friend auto tag_invoke(get_completion_signatures_t, Self&&, const Env&)
          -> compl_sigs<__copy_cvref_t<Self, Source>, Env>;

        template <__decays_to<__t> Self, class Env>
        friend auto tag_invoke(get_sequence_items_t, Self&&, const Env&) -> sequence_items_t;

       public:
        template <__decays_to<Source> Sndr>
        explicit __t(Sndr&& source)
          : source_(static_cast<Sndr&&>(source)) {
        }
      };
    };

    struct repeat_effect_t {
      template <sender_of<set_value_t()> Sender>
        requires __single_typed_sender<Sender>
      auto operator()(Sender&& source) const {
        return __t<sender<__id<decay_t<Sender>>>>{static_cast<Sender&&>(source)};
      }
    };
  } // namespace repeat_each_

  using repeat_effect_::repeat_effect_t;
  inline constexpr repeat_effect_t repeat_effect;
} // namespace gsenders