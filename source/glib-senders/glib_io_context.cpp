#include "glib-senders/glib_io_context.hpp"

#include <stdexcept>

namespace gsenders {

auto operator|(io_condition c1, io_condition c2) noexcept -> io_condition {
  return static_cast<io_condition>(static_cast<int>(c1) | static_cast<int>(c2));
}
auto operator&(io_condition c1, io_condition c2) noexcept -> bool {
  return bool(static_cast<int>(c1) & static_cast<int>(c2));
}

auto glib_scheduler::get_GMainContext() const noexcept -> ::GMainContext* {
  return context_->context_.get();
}

auto tag_invoke(stdexec::schedule_t, glib_scheduler self) noexcept
    -> schedule_sender {
  return schedule_sender{self};
}

auto tag_invoke(exec::schedule_after_t, glib_scheduler self,
                std::chrono::system_clock::duration dur) noexcept -> wait_for_sender {
  auto mil = std::chrono::duration_cast<std::chrono::milliseconds>(dur);
  return wait_for_sender{self.get_GMainContext(), mil};
}

auto tag_invoke(wait_until_t, glib_scheduler self, int fd,
                io_condition condition) noexcept -> wait_until_sender {
  return wait_until_sender{self, fd, condition};
}

glib_scheduler::glib_scheduler() noexcept {
  static glib_io_context ctx{};
  context_ = &ctx;
}

auto glib_io_context::get_scheduler() noexcept -> glib_scheduler {
  return glib_scheduler{*this};
}

void glib_io_context::context_destroy::operator()(
    ::GMainContext* pointer) const noexcept {
  g_main_context_unref(pointer);
}

void glib_io_context::loop_destroy::operator()(
    ::GMainLoop* pointer) const noexcept {
  g_main_loop_unref(pointer);
}

glib_io_context::glib_io_context()
    : glib_io_context(::g_main_context_default()) {}

glib_io_context::glib_io_context(::GMainContext* other) {
  context_.reset(::g_main_context_ref(other));
  if (!context_) {
    throw std::runtime_error("Do not pass nullptr");
  }
  loop_.reset(g_main_loop_new(context_.get(), false));
  if (!loop_) {
    throw std::runtime_error("g_main_loop_new failed");
  }
}

auto glib_io_context::run() -> void { g_main_loop_run(loop_.get()); }

auto glib_io_context::stop() -> void { g_main_loop_quit(loop_.get()); }

} // namespace gsenders