#pragma once

#include "./stream_concepts.hpp"

#include <stdexec/execution.hpp>

namespace gsenders { namespace let_next_ {

  template <class Receiver, class Fun>
  struct operation_base {
    [[no_unique_address]] Receiver rcvr_;
    [[no_unique_address]] Fun fun_;
  };

  template <class Receiver, class Fun>
  class receiver {
    operation_base<Receiver, Fun>& op_;

    template <stdexec::sender Item>
    friend auto tag_invoke(set_next_t, receiver& self, Item&& item) noexcept {
      return stdexec::let_value(static_cast<Item&&>(item), [&self]<class... Args>(Args&&... args) {
        return self.op_.fun_(static_cast<Args&&>(args)...);
      });
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
    friend operation<Sender, std::decay_t<Rcvr>, Fun>
      tag_invoke(stdexec::connect_t, sender&& self, Rcvr&& rcvr) {
      return {
        static_cast<Sender&&>(self.sndr_),
        static_cast<Rcvr&&>(rcvr),
        static_cast<Fun&&>(self.fun_)};
    }
  };
}}