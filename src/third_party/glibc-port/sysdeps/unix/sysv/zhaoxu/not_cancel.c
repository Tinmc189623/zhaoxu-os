/* Non-cancellable wrappers for the port.
 *
 * The kernel has no thread cancellation (clone is not implemented), so these
 * are plain calls to the ordinary entry points.  The openat and pread
 * variants have no kernel counterpart yet and report ENOSYS instead of
 * pretending to work.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/random.h>
#include <unistd.h>

int
__open_nocancel (const char *file, int oflag, ...)
{
  return __open (file, oflag, 0);
}

int
__open64_nocancel (const char *file, int oflag, ...)
{
  return __open64 (file, oflag, 0);
}

int
__openat_nocancel (int fd, const char *file, int oflag, ...)
{
  (void) fd;
  (void) file;
  (void) oflag;
  __set_errno (ENOSYS);
  return -1;
}

int
__openat64_nocancel (int fd, const char *file, int oflag, ...)
{
  return __openat_nocancel (fd, file, oflag);
}

ssize_t
__read_nocancel (int fd, void *buf, size_t nbytes)
{
  return __read (fd, buf, nbytes);
}

ssize_t
__pread64_nocancel (int fd, void *buf, size_t count, off64_t offset)
{
  (void) fd;
  (void) buf;
  (void) count;
  (void) offset;
  __set_errno (ENOSYS);
  return -1;
}

ssize_t
__write_nocancel (int fd, const void *buf, size_t n)
{
  return __write (fd, buf, n);
}

int
__close_nocancel (int fd)
{
  return __close (fd);
}

void
__close_nocancel_nostatus (int fd)
{
  (void) __close (fd);
}

int
__fcntl64_nocancel (int fd, int cmd, ...)
{
  va_list ap;
  va_start (ap, cmd);
  void *arg = va_arg (ap, void *);
  va_end (ap);
  return __fcntl64 (fd, cmd, arg);
}

ssize_t
__getrandom_nocancel (void *buf, size_t buflen, unsigned int flags)
{
  return __getrandom (buf, buflen, flags);
}
