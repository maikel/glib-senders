#include "glib-senders/glib_io_context.hpp"
#include <iostream>
#include <typeinfo>

template <typename Sender, typename Receiver> struct repeat_operation;
template <typename Sender, typename Receiver> struct repeat_receiver {
  static_assert(stdexec::receiver<Receiver>);
  repeat_operation<Sender, Receiver>* op_;
  Receiver* receiver_;

  friend auto tag_invoke(stdexec::set_value_t, repeat_receiver self,
                         bool stop) noexcept -> void {
    if (!stop) {
      self.op_->repeat();
    } else {
      self.op_->set_value();
    }
  }

  friend auto tag_invoke(stdexec::set_stopped_t, repeat_receiver self) noexcept
      -> void {
    self.op_->set_stopped();
  }

  template <typename... Error>
  friend auto tag_invoke(stdexec::set_error_t, repeat_receiver self,
                         Error&&... err) noexcept -> void {
    self.op_->set_error(std::forward<Error>(err)...);
  }

  template <typename Self>
  requires stdexec::__decays_to<Self, repeat_receiver>
  friend auto tag_invoke(stdexec::get_env_t, Self&& self) noexcept {
    return stdexec::get_env(*self.receiver_);
  }
};

template <typename Sender, typename Receiver> struct repeat_operation {
  Sender sender_;
  Receiver receiver_;

  using intermediate_op_t = decltype(stdexec::connect(
      std::declval<Sender&&>(),
      std::declval<repeat_receiver<Sender, Receiver>&&>()));
  union {
    intermediate_op_t buffer;
  } op_;
  bool is_alive_{false};

  auto set_stopped() noexcept -> void {
    if (std::exchange(is_alive_, false)) {
      op_.buffer.~intermediate_op_t();
    }
    stdexec::set_stopped(std::move(receiver_));
  }

  auto set_value() noexcept -> void {
    if (std::exchange(is_alive_, false)) {
      op_.buffer.~intermediate_op_t();
    }
    try {
      stdexec::set_value(std::move(receiver_));
    } catch (...) {
      stdexec::set_error(std::move(receiver_), std::current_exception());
    }
  }

  template <typename... Error> auto set_error(Error&&... err) noexcept -> void {
    if (std::exchange(is_alive_, false)) {
      op_.buffer.~intermediate_op_t();
    }
    stdexec::set_error(std::move(receiver_), std::forward<Error>(err)...);
  }

  auto repeat() noexcept -> void {
    // Sender sender{sender_};
    if (std::exchange(is_alive_, false)) {
      op_.buffer.~intermediate_op_t();
    }
    // Sender sender = sender_;
    new (&op_.buffer) intermediate_op_t{stdexec::connect(
        std::move(sender_), repeat_receiver<Sender, Receiver>{this, &receiver_})};
    is_alive_ = true;
    stdexec::start(op_.buffer);
  }

  friend void tag_invoke(stdexec::start_t, repeat_operation& self) noexcept {
    self.repeat();
  }
};

template <typename Sender> struct repeat_sender {
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(),
                                     stdexec::set_error_t(std::exception_ptr),
                                     stdexec::set_stopped_t()>;

  Sender sender_;

  template <typename Receiver>
  friend auto tag_invoke(stdexec::connect_t, repeat_sender self,
                         Receiver&& r) noexcept {
    static_assert(stdexec::sender_to<Sender, Receiver>);
    return repeat_operation<Sender, std::remove_cvref_t<Receiver>>{
        std::move(self).sender_, std::forward<Receiver>(r)};
  }

  template <typename Self>
  requires stdexec::__decays_to<Self, repeat_sender>
  friend auto tag_invoke(stdexec::get_attrs_t, Self&& self) {
    return tag_invoke(stdexec::get_attrs_t{}, self.sender_);
  }
};

template <typename Sender> auto repeat_until(Sender&& sender) {
  return repeat_sender{std::forward<Sender>(sender)};
}

template <typename Scheduler> struct basic_file_descriptor {
  Scheduler scheduler_;
  int fd_;
};

using file_descriptor = basic_file_descriptor<gsenders::glib_scheduler>;

auto async_read(file_descriptor fd, std::span<char> buffer) {
  using gsenders::when;
  return gsenders::wait_until(fd.scheduler_, fd.fd_, when::readable) |
         stdexec::then([buffer](int fd) {
           ssize_t nbytes = ::read(fd, buffer.data(), buffer.size());
           if (nbytes == -1) {
             throw std::system_error(errno, std::system_category());
           }
           return buffer.subspan(0, nbytes);
         });
}

int main() {
  gsenders::glib_io_context io_context{};
  gsenders::glib_scheduler scheduler = io_context.get_scheduler();

  int i = 0;

  auto schedule = stdexec::schedule(scheduler);
  auto add_one = schedule | stdexec::then([&i] { i += 1; });

  auto wait_a_second =
      gsenders::wait_for(scheduler, std::chrono::milliseconds(1000));

  auto say_hello = wait_a_second |
                   stdexec::then([] { std::cout << "Hello!\n"; }) |
                   stdexec::let_value([=] { return wait_a_second; }) |
                   stdexec::then([] { std::cout << "World!\n"; });

  auto then_stop = stdexec::then([&io_context]<class... Ts>(Ts&&...) -> void { 
    ((std::cout << typeid(std::remove_cvref_t<Ts>).name() << ", "), ...);
    io_context.stop();
  });

  stdexec::start_detached(stdexec::when_all(add_one, add_one, say_hello) |
                          then_stop);

  assert(i == 0);

  io_context.run();

  assert(i == 2);

  file_descriptor fd{scheduler, STDIN_FILENO};
  char buffer[1024];

  int n = 0;
  auto echo = async_read(fd, buffer) |
              stdexec::then([&n](std::span<char> buf) -> bool {
                std::string_view sv(buf.data(), buf.size());
                n += buf.size();
                std::cout << n << ": " << sv;
                if (n > 10) {
                  return true;
                }
                return false;
              });

  stdexec::start_detached(repeat_until(echo) | then_stop);

  io_context.run();
}