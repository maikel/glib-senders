#include "glib-senders/file_descriptor.hpp"

#include <stdexcept>

#include <unistd.h>
#include <fcntl.h>

namespace gsenders {

safe_file_descriptor::safe_file_descriptor(int fd)
{
  if (fd < 0) {
    return;
  }
  int rc = ::fcntl(fd, F_GETFD);
  if (rc == -1) {
    throw std::system_error(errno, std::system_category());
  }
  fd_ = fd;
}

auto safe_file_descriptor::close() noexcept -> std::error_code {
  if (fd_ < 0) {
    return {};
  }
  int rc = ::close(fd_);
  if (rc == -1) {
    return {errno, std::system_category()};
  }
  fd_ = -1;
  return {};
}

safe_file_descriptor::~safe_file_descriptor() {
  close();
}
} // namespace doko