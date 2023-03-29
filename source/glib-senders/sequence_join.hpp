#pragma once

#include "./sequence_sender_concepts.hpp"

namespace gsenders {
  namespace sequence_join_ {
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
        // requires sequence_sender_to<__copy_cvref_t<Self, Sender>, receiver_t<Receiver>>
        friend auto tag_invoke(connect_t, Self&& self, Receiver&& rcvr_id) {
          return connect(
            __copy_cvref_t<Self, Sender>(self.sndr_),
            receiver_t<Receiver>{static_cast<Receiver&&>(rcvr_id)});
        }
      };
    };

    struct sequence_join_t {
      template <class Sender>
      constexpr auto operator()(Sender&& sndr) const {
        return __t<sender<__id<decay_t<Sender>>>>{static_cast<Sender&&>(sndr)};
      }

      constexpr auto operator()() const noexcept -> __binder_back<sequence_join_t> {
        return {};
      }
    };
  } // namespace sequence_join_

  using sequence_join_::sequence_join_t;
  inline constexpr sequence_join_t sequence_join;
} // namespace gsenders