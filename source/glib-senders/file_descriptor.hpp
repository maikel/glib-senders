#ifndef DOKO_SAFE_FILE_DESCRIPTOR_HPP
#define DOKO_SAFE_FILE_DESCRIPTOR_HPP

#include <span>
#include <system_error>
#include <utility>

#include <stdexec/execution.hpp>

#include "glib-senders/glib_io_context.hpp"

namespace gsenders {

struct async_write_some_t {
  template <class Object>
  requires stdexec::tag_invocable<async_write_some_t, Object, std::span<const char>>
  auto operator()(Object&& io, std::span<const char> buffer) const
      noexcept(stdexec::nothrow_tag_invocable<async_write_some_t, Object,
                                              std::span<char>>) {
    return tag_invoke(async_write_some_t{}, std::forward<Object>(io), buffer);
  }

  template <class S>
  requires stdexec::sender<S>
  auto operator()(S&& sender, std::span<const char> buffer) const noexcept(
      stdexec::nothrow_tag_invocable<async_write_some_t, S, std::span<char>>) {
    return stdexec::let_value(
        std::forward<S>(sender), [buffer]<class T>(T&& io) {
          return tag_invoke(async_write_some_t{}, std::forward<T>(io), buffer);
        });
  }

  template <class S>
  requires stdexec::sender<S>
  auto operator()(S&& sender) const noexcept(
      stdexec::nothrow_tag_invocable<async_write_some_t, S, std::span<const char>>) {
    return stdexec::let_value(
        std::forward<S>(sender), []<class T>(T&& io, std::span<char> buffer) {
          return tag_invoke(async_write_some_t{}, std::forward<T>(io), buffer);
        });
  }

  auto operator()() const noexcept {
    return stdexec::__binder_back<async_write_some_t>{{}, {}, {}};
  }

  auto operator()(std::span<char> buffer) const noexcept {
    return stdexec::__binder_back<async_write_some_t, std::span<char>>{
        {}, {}, {buffer}};
  }
};

struct async_read_some_t {
  template <class Object>
  requires stdexec::tag_invocable<async_read_some_t, Object, std::span<char>>
  auto operator()(Object&& io, std::span<char> buffer) const
      noexcept(stdexec::nothrow_tag_invocable<async_read_some_t, Object,
                                              std::span<char>>) {
    return tag_invoke(async_read_some_t{}, std::forward<Object>(io), buffer);
  }

  template <class S>
  requires stdexec::sender<S>
  auto operator()(S&& sender, std::span<char> buffer) const noexcept(
      stdexec::nothrow_tag_invocable<async_read_some_t, S, std::span<char>>) {
    return stdexec::let_value(
        std::forward<S>(sender), [buffer]<class T>(T&& io) {
          return tag_invoke(async_read_some_t{}, std::forward<T>(io), buffer);
        });
  }

  template <class S>
  requires stdexec::sender<S>
  auto operator()(S&& sender) const noexcept(
      stdexec::nothrow_tag_invocable<async_read_some_t, S, std::span<char>>) {
    return stdexec::let_value(
        std::forward<S>(sender), []<class T>(T&& io, std::span<char> buffer) {
          return tag_invoke(async_read_some_t{}, std::forward<T>(io), buffer);
        });
  }

  auto operator()() const noexcept {
    return stdexec::__binder_back<async_read_some_t>{{}, {}, {}};
  }

  auto operator()(std::span<char> buffer) const noexcept {
    return stdexec::__binder_back<async_read_some_t, std::span<char>>{
        {}, {}, {buffer}};
  }
};
inline constexpr async_read_some_t async_read_some;
inline constexpr async_write_some_t async_write_some;

template <class Scheduler> class basic_file_descriptor {
private:
  [[no_unique_address]] Scheduler scheduler_;
  int fd_;

public:
  basic_file_descriptor() = default;

  basic_file_descriptor(Scheduler scheduler, int fd) noexcept
      : scheduler_(std::move(scheduler)), fd_(fd) {}

  [[nodiscard]] auto get_scheduler() const noexcept -> Scheduler {
    return scheduler_;
  }

  [[nodiscard]] auto get_handle() const noexcept -> int { return fd_; }

  friend auto tag_invoke(async_read_some_t, basic_file_descriptor fd,
                         std::span<char> buffer) {
    return wait_until(fd.scheduler_, fd.fd_, io_condition::is_readable) |
           stdexec::then([buffer](int fd) {
             ssize_t nbytes = ::read(fd, buffer.data(), buffer.size());
             if (nbytes == -1) {
               throw std::system_error(errno, std::system_category());
             }
             return buffer.subspan(0, nbytes);
           });
  }

    friend auto tag_invoke(async_write_some_t, basic_file_descriptor fd,
                           std::span<const char> buffer) {
    return wait_until(fd.scheduler_, fd.fd_, io_condition::is_writeable) |
           stdexec::then([buffer](int fd) {
             ssize_t nbytes = ::write(fd, buffer.data(), buffer.size());
             if (nbytes == -1) {
               throw std::system_error(errno, std::system_category());
             }
             return buffer.subspan(nbytes);
           });
  }
};

using file_descriptor = basic_file_descriptor<glib_scheduler>;

/// @brief A file descriptor that is closed when it goes out of scope.
class safe_file_descriptor {
public:
  /// @brief Create an invalid file descriptor.
  safe_file_descriptor() = default;

  /// @brief Take ownership of a file descriptor.
  ///
  /// The constructor tests if the file descriptor is valid and throws an
  /// exception if it is not.
  ///
  /// @param native_handle the file descriptor to take ownership of
  ///
  /// @throws std::system_error if the file descriptor is invalid
  explicit safe_file_descriptor(int native_handle);

  /// @brief Close the file descriptor if it is valid.
  ///
  /// If the file descriptor is invalid, this function does nothing.
  /// If the close operation falis, the error is ignored but errno is set.
  ///
  /// @throws Nothing.
  ///
  /// @see close() to handle errors.
  ~safe_file_descriptor();

  safe_file_descriptor(const safe_file_descriptor&) = delete;
  safe_file_descriptor& operator=(const safe_file_descriptor&) = delete;

  /// @brief Claim ownership of another file descriptor.
  /// @param other The file descriptor to claim ownership of.
  safe_file_descriptor(safe_file_descriptor&& other) noexcept;

  /// @brief Claim ownership of another file descriptor.
  /// @param other The file descriptor to claim ownership of.
  safe_file_descriptor& operator=(safe_file_descriptor&& other) noexcept;

  /// @brief Get the file descriptor.
  [[nodiscard]] auto get() const noexcept -> int;

  /// @brief Release ownership of the file descriptor.
  [[nodiscard]] auto release() noexcept -> int;

  /// @brief Close the file descriptor and set it to invalid.
  /// @return an error code if the close operation failed
  auto close() noexcept -> std::error_code;

  /// @brief Check if the file descriptor is valid.
  explicit operator bool() const noexcept;

private:
  int fd_{-1};
};

///////////////////////////////////////////////////////////////////////////////
// Implementation

inline safe_file_descriptor::safe_file_descriptor(
    safe_file_descriptor&& other) noexcept
    : fd_{std::exchange(other.fd_, -1)} {}

inline auto
safe_file_descriptor::operator=(safe_file_descriptor&& other) noexcept
    -> safe_file_descriptor& {
  fd_ = std::exchange(other.fd_, -1);
  return *this;
}

inline auto safe_file_descriptor::get() const noexcept -> int { return fd_; }

inline auto safe_file_descriptor::release() noexcept -> int {
  return std::exchange(fd_, -1);
}

inline safe_file_descriptor::operator bool() const noexcept { return fd_ >= 0; }

} // namespace gsenders

#endif