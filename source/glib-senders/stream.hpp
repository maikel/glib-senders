// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

#pragma once

#include <stdexec/execution.hpp>

namespace gsenders
{
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

namespace __all_of {



}

}