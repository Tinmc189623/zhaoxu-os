#ifndef ZHAOXU_CPU_H
#define ZHAOXU_CPU_H

#include <types.h>

struct cpu_info {
    char vendor[13];
    char brand[49];
    u32 family;
    u32 model;
    u32 stepping;
    u32 feature_ecx;
    u32 feature_edx;
    u32 extended_edx;
    u32 logical_cpus;
};

void cpu_init(void);
const struct cpu_info *cpu_get_info(void);
u64 cpu_rdtsc(void);
void cpu_features_string(char *buffer, usize size);

#endif
