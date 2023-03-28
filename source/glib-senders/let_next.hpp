#pragma once

#include "./stream_concepts.hpp"

#include <stdexec/execution.hpp>

namespace gsenders { namespace let_next_ {

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

  template <class Receiver, class Fun>
  class receiver {
    operation_base<Receiver, Fun>& op_;

    template <stdexec::sender Item>
    friend auto tag_invoke(set_next_t, receiver& self, Item&& item) noexcept {
      return set_next(
        self.op_.rcvr_,
        stdexec::let_value(static_cast<Item&&>(item), apply_fun<Fun>{&self.op_.fun_}));
    }

    template <stdexec::__completion_tag Tag, class... Args>
    friend void tag_invoke(Tag complete, receiver&& self, Args&&... args) noexcept {
      complete(static_cast<Receiver&&>(self.op_.rcvr_), static_cast<Args&&>(args)...);
    }
  };

  template <class Sender, class Receiver, class Fun>
  struct operation : operation_base<Receiver, Fun> {
    stdexec::connect_result_t<Sender, receiver<Receiver, Fun>> op_;

    template <stdexec::__decays_to<Sender> Sndr, stdexec::__decays_to<Receiver> Rcvr>
    operation(Sndr&& sndr, Rcvr&& rcvr, Fun fun)
      : operation_base<Receiver, Fun>(static_cast<Rcvr&&>(rcvr), static_cast<Fun&&>(fun))
      , op_(stdexec::connect(sndr, receiver{*this})) {
    }

   private:
    friend void tag_invoke(stdexec::start_t, operation& self) noexcept {
      stdexec::start(self.op_);
    }
  };

  template <class Fun, class Item>
  using let_value_t =
    decltype(stdexec::let_value(stdexec::__declval<Item>(), stdexec::__declval<apply_fun<Fun>>()));

  template <class Sender, class Env, class Fun>
  using next_signatures_t = make_next_signatures<
    Sender,
    Env,
    next_signatures<>,
    stdexec::__mbind_front_q<let_value_t, Fun>>;

  template <class Sender, class Fun>
  struct sender {
    [[no_unique_address]] Sender sndr_;
    [[no_unique_address]] Fun fun_;

    template <stdexec::__decays_to<Sender> Sndr>
    sender(Sndr&& sndr, Fun fun)
      : sndr_{static_cast<Sndr&&>(sndr)}
      , fun_{static_cast<Fun&&>(fun)} {
    }

    template <stdexec::receiver Rcvr>
      requires next_receiver_of<Rcvr, next_signatures_t<Sender, stdexec::env_of_t<Rcvr>, Fun>>
            && next_sender_to<Sender, receiver<Rcvr, Fun>>
    friend operation<Sender, std::decay_t<Rcvr>, Fun>
      tag_invoke(stdexec::connect_t, sender&& self, Rcvr&& rcvr) {
      return {
        static_cast<Sender&&>(self.sndr_),
        static_cast<Rcvr&&>(rcvr),
        static_cast<Fun&&>(self.fun_)};
    }

    template <class Env>
    friend auto tag_invoke(stdexec::get_completion_signatures_t, const sender& self, const Env& env)
      -> stdexec::completion_signatures_of_t<Sender, Env>;

    template <class Env>
    friend auto tag_invoke(get_next_signatures_t, const sender& self, const Env& env)
      -> next_signatures_t<Sender, Env, Fun>;
  };
}}