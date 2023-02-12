#ifndef GSENDERS_CHANNEL_HPP
#define GSENDERS_CHANNEL_HPP

#include "glib-senders/file_descriptor.hpp"

#include <sys/eventfd.h>

namespace gsenders {

template <class T> class channel {
public:
  channel() : eventfd_{::eventfd(0, EFD_CLOEXEC)} {}

  template <class U> auto send(U&& value) noexcept -> stdexec::sender auto{
    return stdexec::just(uint64_t(-1)) //
           | stdexec::let_value([this](uint64_t lock) {
               return async_write_some(eventfd_, std::span{&lock, 1});
             }) //
           | stdexec::then([this, val = (U &&) value](auto x) mutable {
               value_ = (U &&) val;
             });
  }

  auto receive() noexcept -> stdexec::sender auto{
    return stdexec::just(uint64_t{}) //
           | stdexec::let_value([this](uint64_t lock) {
               return async_read_some(eventfd_, std::span{&lock, 1});
             }) //
           | stdexec::then([this](auto) { 
               return (T &&)(value_.value()); 
             });
  }

private:
  safe_file_descriptor eventfd_;
  std::optional<T> value_;
};
} // namespace gsenders

#endif