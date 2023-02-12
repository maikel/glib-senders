#ifndef DOKO_SAFE_FILE_DESCRIPTOR_HPP
#define DOKO_SAFE_FILE_DESCRIPTOR_HPP

#include <span>
#include <system_error>
#include <utility>

#include <stdexec/execution.hpp>

#include "glib-senders/glib_io_context.hpp"

namespace gsenders {

struct async_write_some_t {
  template <class Object, class Buffer>
  requires stdexec::tag_invocable<async_write_some_t, Object, Buffer>
  auto operator()(Object&& io, Buffer&& buffer) const noexcept(
      stdexec::nothrow_tag_invocable<async_write_some_t, Object, Buffer>) {
    return tag_invoke(async_write_some_t{}, (Object &&) io, (Buffer &&) buffer);
  }

  template <class S, class Buffer>
  requires stdexec::sender<S>
  auto operator()(S&& sender, Buffer&& buffer) const
      noexcept(stdexec::nothrow_tag_invocable<async_write_some_t, S, Buffer>) {
    return stdexec::let_value(std::forward<S>(sender), [buffer]<class T>(
                                                           T&& io) {
      return tag_invoke(async_write_some_t{}, (T &&) io, (Buffer &&) buffer);
    });
  }
};

struct async_read_some_t {
  template <class Object, class Buffer>
  requires stdexec::tag_invocable<async_read_some_t, Object, Buffer>
  auto operator()(Object&& io, Buffer&& buffer) const noexcept(
      stdexec::nothrow_tag_invocable<async_read_some_t, Object, Buffer>) {
    return tag_invoke(async_read_some_t{}, std::forward<Object>(io),
                      (Buffer &&) buffer);
  }

  template <class S, class Buffer>
  requires stdexec::sender<S>
  auto operator()(S&& sender, Buffer&& buffer) const
      noexcept(stdexec::nothrow_tag_invocable<async_read_some_t, S, Buffer>) {
    return stdexec::let_value(
        std::forward<S>(sender), [buffer]<class T>(T&& io) {
          return tag_invoke(async_read_some_t{}, std::forward<T>(io),
                            (Buffer &&) buffer);
        });
  }
};
inline constexpr async_read_some_t async_read_some;
inline constexpr async_write_some_t async_write_some;

template <class Scheduler> class basic_file_descriptor {
private:
  [[no_unique_address]] Scheduler scheduler_;
  int fd_;

public:
  explicit basic_file_descriptor(int fd) noexcept
  requires std::is_default_constructible_v<Scheduler>
      : scheduler_(Scheduler()), fd_(fd) {}

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
             ssize_t nbytes = ::read(fd, buffer.data(), buffer.size_bytes());
             if (nbytes == -1) {
               throw std::system_error(errno, std::system_category());
             }
             return buffer.subspan(0, nbytes);
           });
  }

  template <class T>
  requires(!std::same_as<T, char>)
  friend auto tag_invoke(async_read_some_t tag, basic_file_descriptor fd,
                         std::span<T> buffer) {
    return tag_invoke(tag, fd, std::span{reinterpret_cast<char*>(buffer.data()), buffer.size_bytes()});
  }

  friend auto tag_invoke(async_write_some_t, basic_file_descriptor fd,
                         std::span<const char> buffer) {
    return wait_until(fd.scheduler_, fd.fd_, io_condition::is_writeable) |
           stdexec::then([buffer](int fd) {
             ssize_t nbytes = ::write(fd, buffer.data(), buffer.size_bytes());
             if (nbytes == -1) {
               throw std::system_error(errno, std::system_category());
             }
             return buffer.subspan(nbytes);
           });
  }

  template <class T>
  requires(!std::same_as<T, const char>)
  friend auto tag_invoke(async_write_some_t tag, basic_file_descriptor fd,
                         std::span<T> buffer) {
    return tag_invoke(tag, fd, std::span{reinterpret_cast<const char*>(buffer.data()), buffer.size_bytes()});
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

  /// @brief Take ownership of a file descriptor.
  ///
  /// The constructor tests if the file descriptor is valid and throws an
  /// exception if it is not.
  ///
  /// @param native_handle the file descriptor to take ownership of
  ///
  /// @throws std::system_error if the file descriptor is invalid
  explicit safe_file_descriptor(file_descriptor fd);

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
  file_descriptor fd_{-1};

  template <class Tag, class... Args>
  requires stdexec::tag_invocable<Tag, file_descriptor&, Args...>
  friend auto tag_invoke(Tag tag, safe_file_descriptor& self, Args&&... args) {
    return stdexec::tag_invoke(tag, self.fd_, (Args &&) args...);
  }
};

///////////////////////////////////////////////////////////////////////////////
// Implementation

inline safe_file_descriptor::safe_file_descriptor(
    safe_file_descriptor&& other) noexcept
    : fd_{std::exchange(other.fd_, file_descriptor(-1))} {}

inline auto
safe_file_descriptor::operator=(safe_file_descriptor&& other) noexcept
    -> safe_file_descriptor& {
  fd_ = std::exchange(other.fd_, file_descriptor(-1));
  return *this;
}

inline auto safe_file_descriptor::get() const noexcept -> int {
  return fd_.get_handle();
}

inline auto safe_file_descriptor::release() noexcept -> int {
  return std::exchange(fd_, file_descriptor(-1)).get_handle();
}

inline safe_file_descriptor::operator bool() const noexcept {
  return fd_.get_handle() >= 0;
}

} // namespace gsenders

#endif