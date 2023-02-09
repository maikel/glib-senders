// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

#pragma once

#include <stdexec/execution.hpp>
#include <ranges>

namespace gsenders
{
using stdexec::__t;

namespace __streams {

struct next_t {
  template <class Stream>
    requires stdexec::tag_invocable<next_t, Stream>
  stdexec::sender auto operator()(Stream&& stream) const noexcept {
    static_assert(stdexec::nothrow_tag_invocable<next_t, Stream>);
    return stdexec::tag_invoke(next_t{}, (Stream &&) stream);
  }
};

struct cleanup_t {
  template <class Stream>
    requires stdexec::tag_invocable<cleanup_t, Stream>
  stdexec::sender auto operator()(Stream&& stream) const noexcept {
    static_assert(stdexec::nothrow_tag_invocable<cleanup_t, Stream>);
    return stdexec::tag_invoke(cleanup_t{}, (Stream &&) stream);
  }
};

}

using __streams::next_t;
using __streams::cleanup_t;

inline constexpr next_t next;
inline constexpr cleanup_t cleanup;


template <class Stream>
concept stream = requires (Stream&& s) {
  { next(s) } -> stdexec::sender;
  { cleanup(s) } -> stdexec::sender;
};

template <class Stream>
  requires stream<Stream>
struct next_sender {
  using __t = std::decay_t<decltype(gsenders::next(std::declval<Stream&>()))>;
};

template <class Stream>
using next_sender_t = __t<next_sender<Stream>>;

namespace __all_of {

template <class Stream>
struct iterator {
  Stream* stream_;

  iterator& operator++() const noexcept { return *this;  }

  iterator operator++(int) const noexcept { return *this; }

  next_sender_t<Stream> operator*() const noexcept {
    return next(*stream_);
  }

  friend auto operator<=>(const iterator&, const iterator&) const noexcept = default;
};
static_assert(std::input_iterator<iterator>);

}

}