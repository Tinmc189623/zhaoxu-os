/*
 * Zhaoxu OS - POSIX/libc shim for the stock libwayland client.
 *
 * The client library is compiled against the host's Linux headers so the
 * public types keep their upstream layout.  This file supplies the libc,
 * errno, pthread and socket entry points it expects on top of ZOS syscalls.
 * It is single-threaded; mutexes and condition variables are safe no-ops
 * that preserve the call sequence of the upstream library.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define ZOS_SYS_EXIT   0
#define ZOS_SYS_WRITE  1
#define ZOS_SYS_READ   4
#define ZOS_SYS_CLOSE  5
#define ZOS_SYS_UPTIME 6
#define ZOS_SYS_SOCKET 43
#define ZOS_SYS_CONNECT 47
#define ZOS_SYS_SENDMSG 48
#define ZOS_SYS_RECVMSG 49
#define ZOS_SYS_POLL   50
#define ZOS_SYS_FCNTL  56

static inline long zos_syscall1(long number, long a)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a)
                     : "memory");
    return result;
}

static inline long zos_syscall2(long number, long a, long b)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b)
                     : "memory");
    return result;
}

static inline long zos_syscall3(long number, long a, long b, long c)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "memory");
    return result;
}

static inline long zos_syscall4(long number, long a, long b, long c, long d)
{
    long result;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "memory");
    return result;
}

/* errno ------------------------------------------------------------------ */

int *__errno_location(void)
{
    static int value;
    return &value;
}

/* heap ------------------------------------------------------------------- */

#define COMPAT_HEAP_BYTES (4u * 1024u * 1024u)
#define COMPAT_ALIGN 16u

struct compat_block {
    size_t size;
    int free;
    unsigned int padding;
    struct compat_block *next;
};

static unsigned char g_heap[COMPAT_HEAP_BYTES] __attribute__((aligned(16)));
static struct compat_block *g_first;

static size_t align_up(size_t value)
{
    return (value + COMPAT_ALIGN - 1) & ~(size_t)(COMPAT_ALIGN - 1);
}

static void heap_setup(void)
{
    if (g_first)
        return;
    g_first = (struct compat_block *)g_heap;
    g_first->size = COMPAT_HEAP_BYTES;
    g_first->free = 1;
    g_first->next = NULL;
}

void *malloc(size_t size)
{
    heap_setup();
    if (size == 0)
        size = 1;
    size = align_up(size);
    for (struct compat_block *block = g_first; block; block = block->next) {
        if (!block->free || block->size < size + sizeof(*block))
            continue;
        if (block->size >= size + sizeof(*block) + COMPAT_ALIGN) {
            struct compat_block *split =
                (struct compat_block *)((unsigned char *)block +
                                        sizeof(*block) + size);
            split->size = block->size - sizeof(*block) - size;
            split->free = 1;
            split->next = block->next;
            block->next = split;
            block->size = sizeof(*block) + size;
        }
        block->free = 0;
        return (unsigned char *)block + sizeof(*block);
    }
    return NULL;
}

void free(void *pointer)
{
    if (!pointer)
        return;
    struct compat_block *block =
        (struct compat_block *)((unsigned char *)pointer - sizeof(*block));
    block->free = 1;
    for (block = g_first; block && block->next; block = block->next) {
        if (block->free && block->next->free) {
            block->size += block->next->size;
            block->next = block->next->next;
        }
    }
}

void *calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-1 / size)
        return NULL;
    size_t total = count * size;
    void *result = malloc(total);
    if (result)
        memset(result, 0, total);
    return result;
}

void *realloc(void *pointer, size_t size)
{
    if (!pointer)
        return malloc(size);
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    struct compat_block *block =
        (struct compat_block *)((unsigned char *)pointer - sizeof(*block));
    size_t old = block->size - sizeof(*block);
    if (old >= size)
        return pointer;
    void *result = malloc(size);
    if (!result)
        return NULL;
    memcpy(result, pointer, old);
    free(pointer);
    return result;
}

char *strdup(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy)
        memcpy(copy, text, length);
    return copy;
}

char *strndup(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length])
        length++;
    char *copy = malloc(length + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

/* string ----------------------------------------------------------------- */

void *memcpy(void *destination, const void *source, size_t length)
{
    unsigned char *dst = destination;
    const unsigned char *src = source;
    for (size_t i = 0; i < length; i++)
        dst[i] = src[i];
    return destination;
}

void *__memcpy_chk(void *destination, const void *source, size_t length,
                   size_t destination_length)
{
    (void)destination_length;
    return memcpy(destination, source, length);
}

void *memmove(void *destination, const void *source, size_t length)
{
    unsigned char *dst = destination;
    const unsigned char *src = source;
    if (dst < src) {
        for (size_t i = 0; i < length; i++)
            dst[i] = src[i];
    } else {
        for (size_t i = length; i > 0; i--)
            dst[i - 1] = src[i - 1];
    }
    return destination;
}

void *memset(void *destination, int value, size_t length)
{
    unsigned char *dst = destination;
    for (size_t i = 0; i < length; i++)
        dst[i] = (unsigned char)value;
    return destination;
}

void __stack_chk_fail(void)
{
    abort();
}

struct cmsghdr *__cmsg_nxthdr(struct msghdr *message, struct cmsghdr *cmsg)
{
    if (!message || !cmsg)
        return NULL;
    unsigned char *next = (unsigned char *)cmsg + CMSG_ALIGN(cmsg->cmsg_len);
    unsigned char *end =
        (unsigned char *)message->msg_control + message->msg_controllen;
    if (next + CMSG_ALIGN(sizeof(struct cmsghdr)) > end)
        return NULL;
    return (struct cmsghdr *)next;
}

int memcmp(const void *first, const void *second, size_t length)
{
    const unsigned char *a = first;
    const unsigned char *b = second;
    for (size_t i = 0; i < length; i++) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    }
    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length])
        length++;
    return length;
}

int strcmp(const char *first, const char *second)
{
    while (*first && *first == *second) {
        first++;
        second++;
    }
    return (unsigned char)*first - (unsigned char)*second;
}

int strncmp(const char *first, const char *second, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        if (first[i] != second[i])
            return (unsigned char)first[i] - (unsigned char)second[i];
        if (!first[i])
            break;
    }
    return 0;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    while ((*destination++ = *source++))
        ;
    return result;
}

#undef strstr
char *strstr(const char *haystack, const char *needle)
{
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *a = haystack;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b)
            return (char *)haystack;
    }
    return NULL;
}

char *strerror(int error)
{
    (void)error;
    return "ZOS error";
}

int unsetenv(const char *name)
{
    (void)name;
    return 0;
}

FILE *open_memstream(char **pointer, size_t *size)
{
    if (pointer)
        *pointer = NULL;
    if (size)
        *size = 0;
    return NULL;
}

int fclose(FILE *stream)
{
    (void)stream;
    return 0;
}

/* libffi's Windows EFI entry points are unreachable on the SysV ABI. */
void ffi_call_efi64(void)
{
    abort();
}

void ffi_prep_cif_machdep_efi64(void)
{
    abort();
}

/* formatting ------------------------------------------------------------- */

FILE *stdout = (FILE *)1;
FILE *stderr = (FILE *)2;

static void emit_char(char *buffer, size_t capacity, size_t *length, char value)
{
    if (*length + 1 < capacity)
        buffer[*length] = value;
    (*length)++;
}

static void emit_number(char *buffer, size_t capacity, size_t *length,
                        unsigned long long value, unsigned base, int width,
                        char pad, int uppercase)
{
    char digits[32];
    const char *alphabet = uppercase ? "0123456789ABCDEF"
                                     : "0123456789abcdef";
    int count = 0;
    if (value == 0)
        digits[count++] = '0';
    while (value) {
        digits[count++] = alphabet[value % base];
        value /= base;
    }
    for (int i = count; i < width; i++)
        emit_char(buffer, capacity, length, pad);
    while (count > 0)
        emit_char(buffer, capacity, length, digits[--count]);
}

static int compat_vformat(char *buffer, size_t capacity, const char *format,
                          va_list arguments)
{
    size_t length = 0;
    if (!buffer || capacity == 0 || !format)
        return 0;
    while (*format) {
        if (*format != '%') {
            emit_char(buffer, capacity, &length, *format++);
            continue;
        }
        format++;
        char pad = ' ';
        int width = 0;
        if (*format == '0') {
            pad = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        int length_modifier = 0;
        while (*format == 'l' || *format == 'z') {
            if (*format == 'l')
                length_modifier++;
            format++;
        }
        char spec = *format ? *format++ : 0;
        if (spec == 's') {
            const char *text = va_arg(arguments, const char *);
            if (!text)
                text = "(null)";
            while (*text)
                emit_char(buffer, capacity, &length, *text++);
        } else if (spec == 'c') {
            emit_char(buffer, capacity, &length,
                      (char)va_arg(arguments, int));
        } else if (spec == 'd' || spec == 'i') {
            long long value = length_modifier >= 2
                                  ? va_arg(arguments, long long)
                                  : length_modifier == 1
                                        ? va_arg(arguments, long)
                                        : va_arg(arguments, int);
            if (value < 0) {
                emit_char(buffer, capacity, &length, '-');
                value = -value;
            }
            emit_number(buffer, capacity, &length, (unsigned long long)value,
                        10, width, pad, 0);
        } else if (spec == 'u' || spec == 'x' || spec == 'X') {
            unsigned long long value =
                length_modifier >= 2
                    ? va_arg(arguments, unsigned long long)
                    : length_modifier == 1
                          ? va_arg(arguments, unsigned long)
                          : va_arg(arguments, unsigned int);
            emit_number(buffer, capacity, &length, value,
                        spec == 'u' ? 10 : 16, width, pad, spec == 'X');
        } else if (spec == 'p') {
            emit_char(buffer, capacity, &length, '0');
            emit_char(buffer, capacity, &length, 'x');
            emit_number(buffer, capacity, &length,
                        (unsigned long long)(uintptr_t)va_arg(arguments, void *),
                        16, (int)(sizeof(void *) * 2), '0', 0);
        } else if (spec == '%') {
            emit_char(buffer, capacity, &length, '%');
        } else if (spec) {
            emit_char(buffer, capacity, &length, spec);
        }
    }
    if (length >= capacity)
        length = capacity - 1;
    buffer[length] = 0;
    return (int)length;
}

int vsnprintf(char *buffer, size_t capacity, const char *format,
              va_list arguments)
{
    return compat_vformat(buffer, capacity, format, arguments);
}

int snprintf(char *buffer, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = compat_vformat(buffer, capacity, format, arguments);
    va_end(arguments);
    return result;
}

int vfprintf(FILE *stream, const char *format, va_list arguments)
{
    (void)stream;
    char buffer[1024];
    int length = compat_vformat(buffer, sizeof(buffer), format, arguments);
    if (length > 0)
        zos_syscall3(ZOS_SYS_WRITE, 2, (long)buffer, length);
    return length;
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    char buffer[1024];
    int length = compat_vformat(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length > 0)
        zos_syscall3(ZOS_SYS_WRITE, 1, (long)buffer, length);
    return length;
}

long strtol(const char *text, char **end, int base)
{
    if (base == 0)
        base = 10;
    long value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * base + (*text - '0');
        text++;
    }
    if (end)
        *end = (char *)text;
    return value;
}

/* environment ------------------------------------------------------------ */

char *getenv(const char *name)
{
    if (!name)
        return NULL;
    if (strcmp(name, "XDG_RUNTIME_DIR") == 0)
        return "/run";
    return NULL;
}

/* process ---------------------------------------------------------------- */

void abort(void)
{
    static const char message[] = "abort\n";
    zos_syscall3(ZOS_SYS_WRITE, 2, (long)message, (long)sizeof(message) - 1);
    zos_syscall1(ZOS_SYS_EXIT, 1);
    for (;;)
        ;
}

void exit(int status)
{
    zos_syscall1(ZOS_SYS_EXIT, status);
    for (;;)
        ;
}

void __assert_fail(const char *assertion, const char *file, unsigned int line,
                   const char *function)
{
    char buffer[256];
    int length = snprintf(buffer, sizeof(buffer), "%s:%u: %s: assertion %s\n",
                          file, line, function, assertion);
    if (length > 0)
        zos_syscall3(ZOS_SYS_WRITE, 2, (long)buffer, length);
    abort();
}

/* sockets ---------------------------------------------------------------- */

static int set_errno_from_result(long result)
{
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)result;
}

int socket(int domain, int type, int protocol)
{
    int nonblock = (type & SOCK_NONBLOCK) != 0;
    type &= ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
    long fd = zos_syscall3(ZOS_SYS_SOCKET, domain, type, protocol);
    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }
    if (nonblock)
        zos_syscall3(ZOS_SYS_FCNTL, fd, F_SETFL, O_NONBLOCK);
    return (int)fd;
}

int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    long result = zos_syscall3(ZOS_SYS_CONNECT, fd, (long)address, length);
    if (result < 0) {
        errno = ECONNREFUSED;
        return -1;
    }
    return 0;
}

int close(int fd)
{
    return set_errno_from_result(zos_syscall1(ZOS_SYS_CLOSE, fd));
}

int shutdown(int fd, int how)
{
    (void)fd;
    (void)how;
    return 0;
}

ssize_t read(int fd, void *buffer, size_t length)
{
    long result = zos_syscall3(ZOS_SYS_READ, fd, (long)buffer, (long)length);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t write(int fd, const void *buffer, size_t length)
{
    long result = zos_syscall3(ZOS_SYS_WRITE, fd, (long)buffer, (long)length);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (ssize_t)result;
}

int dup(int fd)
{
    return set_errno_from_result(
        zos_syscall3(ZOS_SYS_FCNTL, fd, F_DUPFD, 0));
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
    long result =
        zos_syscall3(ZOS_SYS_SENDMSG, fd, (long)message, (long)flags);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    long result =
        zos_syscall3(ZOS_SYS_RECVMSG, fd, (long)message, (long)flags);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (ssize_t)result;
}

int poll(struct pollfd *fds, nfds_t count, int timeout)
{
    long result = zos_syscall3(ZOS_SYS_POLL, (long)fds, (long)count, timeout);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)result;
}

int fcntl(int fd, int command, ...)
{
    va_list arguments;
    va_start(arguments, command);
    long argument = 0;
    if (command == F_DUPFD || command == F_SETFD || command == F_SETFL)
        argument = va_arg(arguments, long);
    va_end(arguments);

    if (command == F_DUPFD_CLOEXEC)
        command = F_DUPFD;
    return set_errno_from_result(
        zos_syscall3(ZOS_SYS_FCNTL, fd, command, argument));
}

/* time ------------------------------------------------------------------- */

int clock_gettime(clockid_t clock, struct timespec *value)
{
    (void)clock;
    if (!value) {
        errno = EINVAL;
        return -1;
    }
    long milliseconds = zos_syscall1(ZOS_SYS_UPTIME, 0);
    value->tv_sec = milliseconds / 1000;
    value->tv_nsec = (milliseconds % 1000) * 1000000L;
    return 0;
}

/* pthread ---------------------------------------------------------------- */

int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attributes)
{
    (void)attributes;
    if (mutex)
        *(volatile int *)mutex = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (!mutex)
        return 0;
    while (__atomic_exchange_n((volatile int *)mutex, 1, __ATOMIC_ACQUIRE))
        while (__atomic_load_n((volatile int *)mutex, __ATOMIC_RELAXED))
            __asm__ volatile("pause");
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex)
        __atomic_store_n((volatile int *)mutex, 0, __ATOMIC_RELEASE);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_cond_init(pthread_cond_t *condition,
                      const pthread_condattr_t *attributes)
{
    (void)condition;
    (void)attributes;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
    (void)condition;
    (void)mutex;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *condition)
{
    (void)condition;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *condition)
{
    (void)condition;
    return 0;
}
