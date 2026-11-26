#ifndef ZHAOXU_SYS_TIMEX_H
#define ZHAOXU_SYS_TIMEX_H

#include <sys/types.h>

struct timex {
    unsigned int modes;
    long offset;
    long freq;
    long maxerror;
    long esterror;
    long status;
    long constant;
    long precision;
    long tolerance;
    struct timeval time;
    long tick;
    long ppsfreq;
    long jitter;
    int shift;
    long stabil;
    long jitcnt;
    long calcnt;
    long errcnt;
    long stbcnt;
};

#define ADJ_OFFSET 0x0001
#define ADJ_FREQUENCY 0x0002
#define ADJ_STATUS 0x0010
#define ADJ_TICK 0x4000
#define ADJ_OFFSET_SINGLESHOT 0x8001

int adjtimex(struct timex *buffer);

#endif
