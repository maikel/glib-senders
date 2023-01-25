#ifndef DOKO_SAFE_FILE_DESCRIPTOR_HPP
#define DOKO_SAFE_FILE_DESCRIPTOR_HPP

#include <system_error>
#include <utility>

namespace gsenders {

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