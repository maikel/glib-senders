#include "glib-senders/file_descriptor.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>

int main()
{
  gsenders::safe_file_descriptor fd{::eventfd(0, EFD_CLOEXEC)};
  if (!fd) {
    return 1;
  }
  uint64_t value = 1;
  ::write(fd.get(), &value, sizeof(value));
  value = 0;
  ::read(fd.get(), &value, sizeof(value));

  ::printf("value: %lu\n", value);

  return 0;
}