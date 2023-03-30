#pragma once

#include "./sequence_sender_concepts.hpp"

namespace gsenders {
  namespace join_all_ {
    using namespace stdexec;

    template <class ReceiverId>
    struct receiver {
      using Receiver = stdexec::__t<ReceiverId>;

      struct __t {
        Receiver rcvr_;

        template <class Item>
        friend Item&& tag_invoke(set_next_t, __t&, Item&& item) noexcept {
          return static_cast<Item&&>(item);
        }

        template <class Error>
        friend void tag_invoke(set_error_t, __t&& self, Error&& error) noexcept {
          set_error(static_cast<Receiver&&>(self.rcvr_), static_cast<Error&&>(error));
        }

        template <__one_of<set_stopped_t, set_value_t> Tag>
        friend void tag_invoke(Tag complete, __t&& self) noexcept {
          complete(static_cast<Receiver&&>(self.rcvr_));
        }

        friend env_of_t<Receiver> tag_invoke(get_env_t, const __t& self) noexcept {
          return get_env(self.rcvr_);
        }
      };
    };

    template <class Rcvr>
    using receiver_t = __t<receiver<__id<decay_t<Rcvr>>>>;

    template <class SenderId>
    struct sender {
      using Sender = stdexec::__t<decay_t<SenderId>>;

      struct __t {
        using completion_signatures = stdexec::
          completion_signatures<set_value_t(), set_error_t(std::exception_ptr), set_stopped_t()>;

        Sender sndr_;

        template <__decays_to<__t> Self, class Receiver>
        // requires receiver_of<receiver_t<Receiver>, completion_signatures_of_t<__copy_cvref_t<Self, Sender>, env_of_t<Receiver>>
        friend __call_result_t<connect_t, __copy_cvref_t<Self, Sender>, receiver_t<Receiver>>
        tag_invoke(connect_t, Self&& self, Receiver&& rcvr) {
          using result_t = tag_invoke_result_t<connect_t, __copy_cvref_t<Self, Sender>, receiver_t<Receiver>>;
          static_assert(operation_state<result_t>);
          return connect(
            __copy_cvref_t<Self, Sender>(self.sndr_),
            receiver_t<Receiver>{static_cast<Receiver&&>(rcvr)});
        }
      };
    };

    struct join_all_t {
      template <class Sender>
      constexpr auto operator()(Sender&& sndr) const {
        return __t<sender<__id<decay_t<Sender>>>>{static_cast<Sender&&>(sndr)};
      }

      constexpr auto operator()() const noexcept -> __binder_back<join_all_t> {
        return {};
      }
    };
  } // namespace join_all_

  using join_all_::join_all_t;
  inline constexpr join_all_t join_all;
} // namespace gsenders