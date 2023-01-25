#include "glib-senders/glib_io_context.hpp"

#include <stdexcept>

namespace gsenders {

void glib_io_context::context_destroy::operator()(
    ::GMainContext* pointer) const noexcept {
  g_main_context_unref(pointer);
}

void glib_io_context::loop_destroy::operator()(
    ::GMainLoop* pointer) const noexcept {
  g_main_loop_unref(pointer);
}

glib_io_context::glib_io_context() {
  context_.reset(::g_main_context_new());
  if (!context_) {
    throw std::runtime_error("g_main_context_new failed");
  }
  loop_.reset(g_main_loop_new(context_.get(), false));
  if (!loop_) {
    throw std::runtime_error("g_main_loop_new failed");
  }
}

auto glib_io_context::run() -> void { g_main_loop_run(loop_.get()); }

auto glib_io_context::stop() -> void { g_main_loop_quit(loop_.get()); }

} // namespace gsenders