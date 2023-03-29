#pragma once

#include <stdexec/execution.hpp>

namespace gsenders {

  struct set_next_t {
    template <class _Fn, class... _Args>
    using __f = stdexec::__minvoke<_Fn, _Args...>;

    template <stdexec::receiver Receiver, stdexec::sender Item>
      requires stdexec::tag_invocable<set_next_t, Receiver&, Item>
            && stdexec::sender< stdexec::tag_invoke_result_t<set_next_t, Receiver&, Item>>
    auto operator()(Receiver& rcvr, Item&& item) const noexcept
      -> stdexec::tag_invoke_result_t<set_next_t, Receiver&, Item> {
      static_assert(stdexec::nothrow_tag_invocable<set_next_t, Receiver&, Item>);
      return tag_invoke(*this, rcvr, (Item&&) item);
    }
  };

  inline constexpr set_next_t set_next{};

  struct get_next_signatures_t {
    template <class Sender, class Env>
      requires stdexec::tag_invocable<get_next_signatures_t, Sender, Env>
    auto operator()(Sender&& sender, Env&& env)
      -> stdexec::tag_invoke_result_t<get_next_signatures_t, Sender, Env> {
      return tag_invoke(*this, static_cast<Sender&&>(sender), static_cast<Env&&>(env));
    }
  };

  namespace next_sig {
    template <std::same_as<set_next_t> Tag, class Ty = stdexec::__q<stdexec::__types>, class Item>
    stdexec::__types<stdexec::__minvoke<Ty, Item>> test(Tag (*)(Item));
    template <class, class = void>
    stdexec::__types<> test(...);
    template <class Tag, class Ty = void, class... Args>
    void test(Tag (*)(Args...) noexcept) = delete;
  } // namespace next_sig

  template <class Signature>
  concept next_signature = stdexec::__typename<decltype(next_sig::test((Signature*) nullptr))>;

  template <next_signature... Signatures>
  struct next_signatures { };

  template <class Signatures>
  inline constexpr bool is_valid_next_signatures_v = false;

  template <class... Sigs>
  inline constexpr bool is_valid_next_signatures_v<next_signatures<Sigs...>> = true;

  template <class Signatures>
  concept valid_next_signatures = is_valid_next_signatures_v<Signatures>;

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
  using missing_next_signature_t = stdexec::__if<
    stdexec::__mbool<stdexec::nothrow_tag_invocable<set_next_t, Receiver&, Item>>,
    found_next_signature,
    _MISSING_NEXT_SIGNATURE_<set_next_t(Item)>>;

  template <class Receiver, class Item>
  auto has_next_signature(set_next_t (*)(Item)) -> missing_next_signature_t<Receiver, Item>;

  template <class Receiver, class... Signatures>
  auto has_next_signatures(next_signatures<Signatures...>*)
    -> decltype((has_next_signature<Receiver>(static_cast<Signatures*>(nullptr)), ...));

  template <class Signatures, class Receiver>
  concept is_valid_next_completions = Signatures::template _WITH_RECEIVER_<Receiver>::value;

  template <class Receiver, class Signatures>
  concept next_receiver_of = stdexec::receiver<Receiver> && requires(Signatures* sigs) {
    {
      has_next_signatures<std::decay_t<Receiver>>(sigs)
    } -> is_valid_next_completions<std::decay_t<Receiver>>;
  };

  template <class Sender, class Env>
  concept with_next_signatures = requires(Sender&& sender, Env&& env) {
    {
      get_next_signatures_t{}(static_cast<Sender&&>(sender), static_cast<Env&&>(env))
    } -> valid_next_signatures;
  };

  template <class Sender, class Env>
  concept next_sender_in = stdexec::sender_in<Sender, Env> && with_next_signatures<Sender, Env>;

  template <class _Sender, class _Env>
  using get_next_signatures_result_t =
    stdexec::__call_result_t<get_next_signatures_t, _Sender, _Env>;

  template <class _Sender, class _Env>
    requires next_sender_in<_Sender, _Env>
  using next_signatures_of_t = get_next_signatures_result_t<_Sender, _Env>;

  template <class Sender, class Receiver>
  concept next_sender_to =
    stdexec::sender_to<Sender, Receiver>                   //
    && next_sender_in<Sender, stdexec::env_of_t<Receiver>> //
    && next_receiver_of<Receiver, next_signatures_of_t<Sender, stdexec::env_of_t<Receiver>>>;

  template <class Item>
  Item next_item_of(set_next_t (*)(Item));

  template <class Signature>
  using next_item_of_t = decltype(next_item_of((Signature*) nullptr));

  template <class Fn, class Sender, class Env>
  using apply_to_sigs = stdexec::__mapply<
    stdexec::__mcompose<
      stdexec::__mconcat<>,
      stdexec::__transform<stdexec::__mcompose<Fn, stdexec::__q<next_item_of_t>>>>,
    next_signatures_of_t<Sender, Env>>;

  template <class Sender, class Env, class NewSigs, class FnSigs>
    requires next_sender_in<Sender, Env>
  using make_next_signatures_t = stdexec::__mapply<
    stdexec::__munique<stdexec::__q<next_signatures>>,
    stdexec::__minvoke<stdexec::__mconcat<>, NewSigs, apply_to_sigs<FnSigs, Sender, Env>>>;

} // namespace gsenders