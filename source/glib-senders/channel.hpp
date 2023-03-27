#pragma once

#include <stdexec/execution.hpp>

#include <atomic>
#include <optional>

namespace gsenders {

template <class Ty> class channel {
  struct channel_operation {
    void (*complete_)(channel_operation*) noexcept = nullptr;
  };

  template <class SendReceiver> class send_operation : channel_operation {
    channel& channel_{};
    Ty value_{};
    SendReceiver rcvr_{};

    void start() noexcept {
      channel_.value_.emplace((Ty&&)value_);
      channel_operation* expected_op = nullptr;
      if (!channel_.op_.compare_exchange_strong(
              expected_op, static_cast<channel_operation*>(this),
              std::memory_order_release, std::memory_order_relaxed)) {
        expected_op->complete_(expected_op);
        stdexec::set_value((SendReceiver&&)rcvr_);
      }
    }

    friend void tag_invoke(stdexec::start_t, send_operation& self) noexcept {
      self.start();
    }

  public:
    template <stdexec::__decays_to<SendReceiver> _Receiver>
    send_operation(channel& channel, Ty&& value, _Receiver&& rcvr)
        : channel_{channel}, value_{(Ty&&)value}, rcvr_{(_Receiver&&)rcvr} {
      this->complete_ = [](channel_operation* op) noexcept {
        send_operation* self = static_cast<send_operation*>(op);
        stdexec::set_value((SendReceiver&&)self->rcvr_);
      };
    }
  };

  class send_sender {
  public:
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(),
                                       stdexec::set_stopped_t()>;

    explicit send_sender(channel& ch, Ty&& value)
        : channel_{&ch}, value_{(Ty&&)value} {}

  private:
    channel* channel_;
    Ty value_;

    template <stdexec::__decays_to<send_sender> _Self,
              stdexec::receiver_of<completion_signatures> _Receiver>
    friend auto tag_invoke(stdexec::connect_t, _Self&& self, _Receiver&& rcvr)
        -> send_operation<std::decay_t<_Receiver>> {
      return send_operation<std::decay_t<_Receiver>>{
          *self.channel_, (Ty&&)self.value_, (_Receiver&&)rcvr};
    }
  };

  template <class ReceiveReceiver> class receive_operation : channel_operation {
    channel& channel_{};
    ReceiveReceiver rcvr_{};

    void start() noexcept {
      channel_operation* expected_op = nullptr;
      if (!channel_.op_.compare_exchange_strong(
              expected_op, static_cast<channel_operation*>(this),
              std::memory_order_acquire, std::memory_order_relaxed)) {
        stdexec::set_value((ReceiveReceiver&&)rcvr_, (Ty&&)*channel_.value_);
        expected_op->complete_(expected_op);
      }
    }

    friend void tag_invoke(stdexec::start_t, receive_operation& self) noexcept {
      self.start();
    }

  public:
    template <stdexec::__decays_to<ReceiveReceiver> _Receiver>
    receive_operation(channel& channel, _Receiver&& rcvr)
        : channel_{channel}, rcvr_{(_Receiver&&)rcvr} {
      this->complete_ = [](channel_operation* op) noexcept {
        receive_operation* self = static_cast<receive_operation*>(op);
        stdexec::set_value((ReceiveReceiver&&)self->rcvr_,
                           (Ty&&)*self->channel_.value_);
      };
    }
  };

  class receive_sender {
  public:
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(Ty&&),
                                       stdexec::set_stopped_t()>;

    explicit receive_sender(channel& ch) noexcept : channel_{&ch} {}

  private:
    channel* channel_;

    template <stdexec::__decays_to<receive_sender> _Self, class _Receiver>
    requires stdexec::receiver_of<_Receiver, completion_signatures>
    friend receive_operation<std::decay_t<_Receiver>>
    tag_invoke(stdexec::connect_t, _Self&& self, _Receiver&& rcvr) {
      return receive_operation<std::decay_t<_Receiver>>{*self.channel_,
                                                        (_Receiver&&)rcvr};
    }
  };

public:
  send_sender send(Ty&& value) noexcept {
    return send_sender{*this, (Ty&&)value};
  }

  send_sender send(const Ty& value) noexcept {
    return send_sender{*this, value};
  }

  receive_sender receive() noexcept { return receive_sender{*this}; }

private:
  std::optional<Ty> value_{};
  std::atomic<channel_operation*> op_{nullptr};
  stdexec::in_place_stop_source __stop_source_{};
};

} // namespace gsenders