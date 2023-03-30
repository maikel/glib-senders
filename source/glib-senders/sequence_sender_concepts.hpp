#pragma once

#include <stdexec/execution.hpp>

namespace gsenders {
  namespace sequence_senders_ {
    using namespace stdexec;

    struct set_next_t {
      template <class Receiver, class Item>
        requires tag_invocable<set_next_t, Receiver&, Item>
      auto operator()(Receiver& rcvr, Item&& item) const noexcept
        -> tag_invoke_result_t<set_next_t, Receiver&, Item> {
        static_assert(sender<tag_invoke_result_t<set_next_t, Receiver&, Item>>);
        static_assert(nothrow_tag_invocable<set_next_t, Receiver&, Item>);
        return tag_invoke(*this, rcvr, (Item&&) item);
      }
    };
  }

  template <stdexec::sender... Items>
  struct sequence_items { };

  namespace sequence_senders_ {

    template <class Items>
    inline constexpr bool is_valid_sequence_items_v = false;

    template <class... Items>
    inline constexpr bool is_valid_sequence_items_v<sequence_items<Items...>> = true;

    template <class Items>
    concept valid_sequence_items = is_valid_sequence_items_v<Items>;

    template <class Signature>
    struct _MISSING_NEXT_SIGNATURE_;

    template <class Item>
    struct _MISSING_NEXT_SIGNATURE_<set_next_t(Item)> {
      template <class Receiver>
      struct _WITH_RECEIVER_ : std::false_type { };

      friend auto operator,(_MISSING_NEXT_SIGNATURE_, auto) -> _MISSING_NEXT_SIGNATURE_ {
        return {};
      }
    };

    struct found_next_signature {
      template <class Receiver>
      using _WITH_RECEIVER_ = std::true_type;
    };

    template <class Receiver, class Item>
    using missing_next_signature_t = __if<
      __mbool<nothrow_tag_invocable<set_next_t, Receiver&, Item>>,
      found_next_signature,
      _MISSING_NEXT_SIGNATURE_<set_next_t(Item)>>;

    template <class Receiver, class Item>
    auto has_next_signature(Item*) -> missing_next_signature_t<Receiver, Item>;

    template <class Receiver, class... Items>
    auto has_sequence_items(sequence_items<Items...>*)
      -> decltype((has_next_signature<Receiver>(static_cast<Items*>(nullptr)), ...));

    template <class Signatures, class Receiver>
    concept is_valid_next_completions = Signatures::template _WITH_RECEIVER_<Receiver>::value;

    struct get_sequence_items_t {
      template <class Sender, class Env>
        // requires tag_invocable<get_sequence_items_t, Sender, Env>
      auto operator()(Sender&& sender, Env&& env)
        -> tag_invoke_result_t<get_sequence_items_t, Sender, Env> {
        return tag_invoke(*this, static_cast<Sender&&>(sender), static_cast<Env&&>(env));
      }
    };

  } // namespace sequence_senders_

  using sequence_senders_::set_next_t;
  using sequence_senders_::get_sequence_items_t;
  inline constexpr set_next_t set_next{};
  inline constexpr get_sequence_items_t get_sequence_items{};

  template <class Receiver, class Signatures>
  concept sequence_receiver_of =
    stdexec::receiver_of<Receiver, stdexec::completion_signatures<stdexec::set_value_t()>>
    && requires(Signatures* sigs) {
         {
           sequence_senders_::has_sequence_items<std::decay_t<Receiver>>(sigs)
         } -> sequence_senders_::is_valid_next_completions<std::decay_t<Receiver>>;
       };

  template <class Sender, class Env>
  concept with_sequence_items = requires(Sender&& sender, Env&& env) {
    {
      get_sequence_items_t{}(static_cast<Sender&&>(sender), static_cast<Env&&>(env))
    } -> sequence_senders_::valid_sequence_items;
  };

  template <class Sender, class Env = stdexec::empty_env>
  concept sequence_sender_in =
    stdexec::sender_of<Sender, stdexec::set_value_t(), Env> //
    && stdexec::__single_typed_sender<Sender, Env>          //
    && with_sequence_items<Sender, Env>;

  template <class Sender>
  concept sequence_sender = sequence_sender_in<Sender>;

  template <class _Sender, class _Env>
  using get_sequence_items_result_t = stdexec::__call_result_t<get_sequence_items_t, _Sender, _Env>;

  template <class _Sender, class _Env>
  using sequence_items_of_t = get_sequence_items_result_t<_Sender, _Env>;

  template <class Sender, class Receiver>
  concept sequence_sender_to =
    sequence_sender_in<Sender, stdexec::env_of_t<Receiver>>
    && stdexec::receiver_of<Receiver, stdexec::completion_signatures<stdexec::set_value_t()>>
    && sequence_receiver_of<Receiver, sequence_items_of_t<Sender, stdexec::env_of_t<Receiver>>>
    && requires(Sender&& __sndr, Receiver&& __rcvr) {
         { stdexec::connect(static_cast<Sender&&>(__sndr), static_cast<Receiver&&>(__rcvr)) } -> stdexec::operation_state;
       };

  template <class Fn, class Sender, class Env>
  using apply_to_sigs = stdexec::__mapply<
    stdexec::__mcompose< stdexec::__mconcat<>, stdexec::__transform<Fn>>,
    sequence_items_of_t<Sender, Env>>;

  template <class Item>
  using detault_next_item = Item;

  template <class Sender, class Env, class NewSigs, class FnSigs = stdexec::__q<detault_next_item>>
    requires sequence_sender_in<Sender, Env>
  using __make_sequence_items_t = stdexec::__mapply<
    stdexec::__munique<stdexec::__q<sequence_items>>,
    stdexec::
      __minvoke<stdexec::__mconcat<>, NewSigs, apply_to_sigs<FnSigs, Sender, Env>>>;

  template <class Sender, class Env, class NewSigs, template <class> class FnSigs>
    requires sequence_sender_in<Sender, Env>
  using make_sequence_items_t = __make_sequence_items_t<Sender, Env, NewSigs, stdexec::__q<FnSigs>>;

} // namespace gsenders