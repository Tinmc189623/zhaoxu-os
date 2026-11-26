/* Kernel log control; the kernel keeps a log ring, this is its reader. */
#ifndef ZHAOXU_SYS_KLOG_H
#define ZHAOXU_SYS_KLOG_H

#include <sys/sysinfo.h>

#define SYSLOG_ACTION_READ_ALL 3
#define SYSLOG_ACTION_READ_CLEAR 4
#define SYSLOG_ACTION_CLEAR 5
#define SYSLOG_ACTION_SIZE_BUFFER 10

int klogctl(int type, char *buffer, int length);

#endif
