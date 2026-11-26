#ifndef ZHAOXU_SYSLOG_H
#define ZHAOXU_SYSLOG_H
#define LOG_PID 0x01
#define LOG_PERROR 0x20
#define LOG_CONS 0x02
#define LOG_USER (1 << 3)
#define LOG_DAEMON (3 << 3)
#define LOG_AUTH (4 << 3)
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7
void openlog(const char *ident, int options, int facility);
void syslog(int priority, const char *format, ...);
void vsyslog(int priority, const char *format, __builtin_va_list arguments);
void closelog(void);
#endif
