#pragma once

#include <stdexec/execution.hpp>

#include <atomic>
#include <optional>

namespace gsenders {

template <class Ty> class channel {
  struct channel_operation {
    void (*complete_)(channel_operation* op,
                      channel_operation* continuation) noexcept = nullptr;
  };

  template <class SendReceiver> class send_operation : channel_operation {
    channel& channel_{};
    Ty value_{};
    SendReceiver rcvr_{};
    struct on_stop_t {
      send_operation& self_;
      void operator()() noexcept {
        channel_operation* self = static_cast<channel_operation*>(&self_);
        if (self_.channel_.op_.compare_exchange_strong(
                self, nullptr, std::memory_order_relaxed)) {
          if (self == static_cast<channel_operation*>(&self_)) {
            stdexec::set_stopped(static_cast<SendReceiver&&>(self_.rcvr_));
          }
        }
      }
    };
    std::optional<stdexec::in_place_stop_callback<on_stop_t>> on_channel_stop_{};
    std::optional<typename stdexec::stop_token_of_t<
        stdexec::env_of_t<SendReceiver>>::template callback_type<on_stop_t>>
        on_receiver_stop_{};

    void start() noexcept {
      channel_operation* expected_op = nullptr;
      channel_.value_.emplace(static_cast<Ty&&>(value_));
      if (!channel_.op_.compare_exchange_strong(
              expected_op, static_cast<channel_operation*>(this),
              std::memory_order_release, std::memory_order_relaxed)) {
        expected_op->complete_(expected_op, nullptr);
        stdexec::set_value(static_cast<SendReceiver&&>(rcvr_));
      }
    }

    friend void tag_invoke(stdexec::start_t, send_operation& self) noexcept {
      self.start();
    }

  public:
    template <stdexec::__decays_to<SendReceiver> _Receiver>
    send_operation(channel& channel, Ty&& value, _Receiver&& rcvr)
        : channel_{channel}, value_{(Ty&&)value}, rcvr_{(_Receiver&&)rcvr} {
      this->complete_ = [](channel_operation* op, channel_operation*) noexcept {
        send_operation* self = static_cast<send_operation*>(op);
        if (self->channel_.op_.compare_exchange_strong(
                op, nullptr, std::memory_order_relaxed)) {
          stdexec::set_value(static_cast<SendReceiver&&>(self->rcvr_));
        }
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

    template <stdexec::__decays_to<send_sender> Self,
              stdexec::receiver_of<completion_signatures> _Receiver>
    friend auto tag_invoke(stdexec::connect_t, Self&& self, _Receiver&& rcvr)
        -> send_operation<std::decay_t<_Receiver>> {
      return send_operation<std::decay_t<_Receiver>>{
          *self.channel_, (stdexec::__copy_cvref_t<Self, Ty>)self.value_,
          (_Receiver&&)rcvr};
    }
  };

  template <class ReceiveReceiver> class receive_operation : channel_operation {
    channel& channel_{};
    ReceiveReceiver rcvr_{};

    void start() noexcept {
      channel_operation* expected_op = nullptr;
      if (!channel_.op_.compare_exchange_strong(
              expected_op, static_cast<channel_operation*>(this),
              std::memory_order_relaxed, std::memory_order_acquire)) {
        expected_op->complete_(expected_op,
                               static_cast<channel_operation*>(this));
      }
    }

    friend void tag_invoke(stdexec::start_t, receive_operation& self) noexcept {
      self.start();
    }

  public:
    template <stdexec::__decays_to<ReceiveReceiver> _Receiver>
    receive_operation(channel& channel, _Receiver&& rcvr)
        : channel_{channel}, rcvr_{(_Receiver&&)rcvr} {
      this->complete_ = [](channel_operation* op, channel_operation*) noexcept {
        receive_operation* self = static_cast<receive_operation*>(op);
        stdexec::set_value(static_cast<ReceiveReceiver&&>(self->rcvr_),
                           static_cast<Ty&&>(*self->channel_.value_));
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
      return receive_operation<std::decay_t<_Receiver>>{
          *self.channel_, static_cast<_Receiver&&>(rcvr)};
    }
  };

  template <class WhenReceiver> class when_ready_operation : channel_operation {
    channel& channel_{};
    WhenReceiver rcvr_;

    void start() noexcept {
      channel_operation* expected_op = nullptr;
      if (!channel_.op_.compare_exchange_strong(
              expected_op, static_cast<channel_operation*>(this),
              std::memory_order_relaxed, std::memory_order_relaxed)) {
        stdexec::set_value(static_cast<WhenReceiver&&>(rcvr_));
      }
    }

    friend void tag_invoke(stdexec::start_t,
                           when_ready_operation& self) noexcept {
      self.start();
    }

  public:
    when_ready_operation(channel& channel, WhenReceiver&& rcvr)
        : channel_{channel}, rcvr_{(WhenReceiver&&)rcvr} {
      this->complete_ = [](channel_operation* op,
                           channel_operation* cont) noexcept {
        when_ready_operation* self = static_cast<when_ready_operation*>(op);
        self->channel_.op_.store(cont, std::memory_order_relaxed);
        stdexec::set_value(static_cast<WhenReceiver&&>(self->rcvr_));
      };
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

  void stop() noexcept { stop_source_.request_stop(); }

private:
  std::optional<Ty> value_{};
  std::atomic<channel_operation*> op_{nullptr};
  stdexec::in_place_stop_source stop_source_{};
};

} // namespace gsenders