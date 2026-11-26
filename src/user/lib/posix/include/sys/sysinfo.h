/* System information from the kernel's real counters. */
#ifndef ZHAOXU_SYS_SYSINFO_H
#define ZHAOXU_SYS_SYSINFO_H

struct sysinfo {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char padding[20 - 2 * sizeof(unsigned long) - sizeof(unsigned int)];
};

int sysinfo(struct sysinfo *info);

#endif
