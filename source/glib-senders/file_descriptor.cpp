#include "glib-senders/file_descriptor.hpp"

#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

namespace gsenders {

safe_file_descriptor::safe_file_descriptor(int fd)
    : safe_file_descriptor(file_descriptor(fd)) {}

safe_file_descriptor::safe_file_descriptor(file_descriptor fd) {
  if (fd.get_handle() < 0) {
    return;
  }
  int rc = ::fcntl(fd.get_handle(), F_GETFD);
  if (rc == -1) {
    throw std::system_error(errno, std::system_category());
  }
  fd_ = fd;
}

auto safe_file_descriptor::close() noexcept -> std::error_code {
  if (fd_.get_handle() < 0) {
    return {};
  }
  int rc = ::close(fd_.get_handle());
  if (rc == -1) {
    return {errno, std::system_category()};
  }
  fd_ = file_descriptor(-1);
  return {};
}

safe_file_descriptor::~safe_file_descriptor() { close(); }
} // namespace gsenders