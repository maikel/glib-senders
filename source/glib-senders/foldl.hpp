#pragma once

#include <stdexec/execution.hpp>

namespace stdexec
{
  namespace __foldl {
    template <class _Receiver, class _Op>
      struct __receiver_id {
        class __t {
        public:

        private:
          _Op* __op_;
        };
      };
    template <class _Receiver, class _Op>
      using __receiver = __t<__receiver_id<std::decay_t<_Receiver>, std::decay_t<_Op>>>;

    template <class _Sequence, class _Init, class _F, class _Receiver>
      struct __op_id {
        class __t {
        public:
          __t(_Sequence&& __seq, _F&& __f, _Init&& __init, _Receiver&& __receiver) noexcept
          : __seq_{(_Sequence&&) __seq}
          , __f_{(_F&&) __f}
          , __init_{(_Init&&) __init_}
          , __receiver_{(_Receiver&&) __receiver} {}

        private:
          _Sequence __seq_;
          _F __f_;
          _Receiver __receiver_;

          __init_value_type_t __current_value_;

          __operation_t __op_;

          bool __f_stopped_{false};
          in_place_stop_source __stop_source_{};

          template <class... _Args>
            void next(_Args&&... __args) noexcept {
              if (get_stop_token(get_env(__receiver_)).stop_requested()) {
                stdexec::set_stopped((_Receiver&&) __receiver_);
              } 
                __op_.emplace(__conv{stdexec::connect(
                  when_all(
                    __seq_, __f_((_Args&&) __args...) 
                            | stdexec::upon_stopped([&__f_stopped_] { __f_stopped_ = true; })), 
                    __receiver<_Receiver, __t>{this})});
                stdexec::start(*__op_);
              } else {

              }
            }

          void start() noexcept {
            __op_.emplace(__conv{stdexec::connect(
                  when_all(__seq_, (_Init&&) __init_), __receiver<_Receiver, __t>{this})});
            stdexec::start(*__op_);
          }
        };
      };

    template <class _Sequence, class _F>
      struct __sender_id {
        class __t {
        public:
          __t(_Sequence&& __seq, _F&& __f) noexcept : __seq_{(_Sequence&&) __seq}, __f_{(_F&&) __f} {}

        private:
          _Sequence __seq_;
          _F __f_;
        };
      };
    
    template <class _Sequence, class _F>
      using __sender = __t<__sender_id<std::decay_t<_Sequence>, std::decay_t<_F>>>;

    struct __foldl_t {
      template <class _Sequence, class _Init, class _F>
          // requires callable F for each set_value_t completion of _Sequence
        friend auto tag_invoke(__foldl_t, _Sequence&& __seq, _Init&& __init, _F&& __f) noexcept {
          return __sender<_Sequence, _F>{(_Sequence&&) __seq, (_Init&&) __init, (_F&&) __f};
        }
    };

    constexpr inline __foldl_t foldl{};
  }

  using __foldl::foldl;
}