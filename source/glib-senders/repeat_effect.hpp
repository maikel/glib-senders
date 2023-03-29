#pragma once

#include "./sequence_sender_concepts.hpp"

#include <stdexec/execution.hpp>

#include <exec/on.hpp>
#include <exec/trampoline_scheduler.hpp>

namespace gsenders {
  namespace repeat_effect_ {
    using namespace stdexec;

    // Takes a sender and creates a sequence sender by repeating the sender as item to the set_next
    // operation.
    template <class SourceSenderId, class ReceiverId>
    struct operation {
      using SourceSender = stdexec::__t<SourceSenderId>;
      using Receiver = stdexec::__t<ReceiverId>;

      using NextSender = __call_result_t<set_next_t, Receiver&, SourceSender>;

      struct __t {
        struct repeat_receiver {
          __t* op_;

          friend void tag_invoke(set_value_t, repeat_receiver&& self) noexcept {
            self.op_->repeat();
          }

          template <class Error>
          friend void tag_invoke(set_error_t, repeat_receiver&& self, Error e) noexcept {
            set_error(static_cast<Receiver&&>(self.op_->rcvr_), static_cast<Error&&>(e));
          }

          friend void tag_invoke(set_stopped_t, repeat_receiver&& self) noexcept {
            set_value(static_cast<Receiver&&>(self.op_->rcvr_));
          }

          friend env_of_t<Receiver> tag_invoke(get_env_t, const repeat_receiver& self) noexcept(
            __nothrow_callable<get_env_t, const Receiver&>) {
            return get_env(self.op_->rcvr_);
          }
        };

        SourceSender source_;
        Receiver rcvr_;
        exec::trampoline_scheduler trampoline_;
        using next_on_scheduler_sender =
          __call_result_t<on_t, exec::trampoline_scheduler, NextSender&&>;

        std::optional<connect_result_t<next_on_scheduler_sender, repeat_receiver>> next_op_;

        void repeat() noexcept {
          auto token = get_stop_token(get_env(rcvr_));
          if (token.stop_requested()) {
            set_stopped(static_cast<Receiver&&>(rcvr_));
            return;
          }
          try {
            auto& next = next_op_.emplace(__conv{[&] {
              return connect(
                stdexec::on(trampoline_, set_next(rcvr_, SourceSender{source_})),
                repeat_receiver{this});
            }});
            start(next);
          } catch (...) {
            set_error(static_cast<Receiver&&>(rcvr_), std::current_exception());
          }
        }

        friend void tag_invoke(start_t, __t& self) noexcept {
          self.repeat();
        }

       public:
        explicit __t(SourceSender source, Receiver rcvr)
          : source_(static_cast<SourceSender&&>(source))
          , rcvr_(static_cast<Receiver&&>(rcvr)) {
        }
      };
    };

    template <class SourceSender, class Receiver>
    using operation_t = __t<operation<__id<SourceSender>, __id<decay_t<Receiver>>>>;

    template <class SourceId>
    struct sender {
      using Source = stdexec::__t<decay_t<SourceId>>;

      class __t {
        using next_signatures_t = next_signatures<set_next_t(Source)>;

        Source source_;

        template <__decays_to<__t> Self, sequence_receiver_of<next_signatures_t> Receiver>
        // requires sequence_sender_to<Self, Receiver>
        friend operation_t<Source, Receiver> tag_invoke(connect_t, Self&& self, Receiver&& rcvr) {
          return operation_t<Source, Receiver>{
            static_cast<Self&&>(self).source_, static_cast<Receiver&&>(rcvr)};
        }

        template <class Env>
        friend auto tag_invoke(get_next_signatures_t, const __t&, const Env&) -> next_signatures_t;

       public:
        using completion_signatures = stdexec::
          completion_signatures<set_value_t(), set_error_t(std::exception_ptr), set_stopped_t()>;

        template <__decays_to<Source> Sndr>
        explicit __t(Sndr&& source)
          : source_(static_cast<Sndr&&>(source)) {
        }
      };
    };

    struct repeat_effect_t {
      // TODO constrain that set_value_t() is the one and only value completion signature
      template <sender_of<set_value_t()> Sender>
      auto operator()(Sender&& source) const {
        return __t<sender<__id<decay_t<Sender>>>>{static_cast<Sender&&>(source)};
      }
    };
  } // namespace repeat_each_

  using repeat_effect_::repeat_effect_t;
  inline constexpr repeat_effect_t repeat_effect;
} // namespace gsenders