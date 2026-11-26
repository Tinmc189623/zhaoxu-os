#!/bin/sh
# Build toybox 0.8.12 for Zhaoxu OS inside WSL2.  Run from src/:
#   wsl -d Ubuntu-26.04 -- bash tools/toybox-build.sh
# Requires clang, lld, bmake/make, ar and the newlib install produced by
# tools/newlib.py at $HOME/newlib/install/x86_64-elf.
# Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
# Archive libregex and build toybox against newlib.  Headers toybox expects
# from glibc but newlib lacks are provided in a spaceless port directory.
set -e
port=/home/tindo/toybox/port-include
mkdir -p "$port/sys"
cp "$SRC/src/third_party/bsd-regex/regex.h" "$port/"
cp "$SRC/src/third_party/bsd-regex-port/include/collate.h" "$port/"
# newlib's x86_64-elf dirent.h has no DIR type; the project's POSIX overlay
# supplies the one ncurses already uses.
cp "$SRC/src/user/lib/posix/include/dirent.h" "$port/"
sed -i 's|^#endif$|DIR *fdopendir(int fd);\n#endif|' "$port/dirent.h"

# Guard the sysconf names: newlib already defines some of them, and a macro
# redefinition would break the build.
awk 'prev !~ /^#ifndef / && /^#define _(SC|CS|PC)_/ { name=$2; print "#ifndef " name; print; print "#endif"; prev=$0; next } { print; prev=$0 }' \
    "$port/unistd.h" > "$port/unistd.h.tmp" && mv "$port/unistd.h.tmp" "$port/unistd.h"

cat > "$port/sys/types.h" <<'EOF'
/* dev_t helpers live beside the types they operate on, as on BSD. */
#ifndef ZHAOXU_SYS_TYPES_OVERLAY_H
#define ZHAOXU_SYS_TYPES_OVERLAY_H

#include_next <sys/types.h>
#include <sys/sysmacros.h>

#endif
EOF

cat > "$port/strings.h" <<'EOF'
#ifndef ZHAOXU_STRINGS_OVERLAY_H
#define ZHAOXU_STRINGS_OVERLAY_H

#include <sys/types.h>
#include_next <strings.h>

char *strcasestr(const char *haystack, const char *needle);
int posix_fallocate(int fd, off_t offset, off_t length);

#endif
EOF

cat > "$port/linux/rfkill.h" <<'EOF'
/* rfkill control surface; no radio hardware support yet, declarations only
 * so the toy builds against the real interface shape. */
#ifndef ZHAOXU_LINUX_RFKILL_H
#define ZHAOXU_LINUX_RFKILL_H

#include <stdint.h>

struct rfkill_event {
    uint32_t idx;
    uint8_t type;
    uint8_t op;
    uint8_t soft;
    uint8_t hard;
};

#define RFKILL_OP_ADD 0
#define RFKILL_OP_DEL 1
#define RFKILL_OP_CHANGE 2
#define RFKILL_OP_CHANGE_ALL 3

#define RFKILL_TYPE_ALL 0
#define RFKILL_TYPE_WLAN 1
#define RFKILL_TYPE_BLUETOOTH 2
#define RFKILL_TYPE_UWB 3
#define RFKILL_TYPE_WIMAX 4
#define RFKILL_TYPE_WWAN 5
#define RFKILL_TYPE_GPS 6
#define RFKILL_TYPE_FM 7

#endif
EOF

cat > "$port/sys/timex.h" <<'EOF'
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
EOF

cat > "$port/linux/fs.h" <<'EOF'
/* Block-layer ioctls used by blockdev/fs helpers. */
#ifndef ZHAOXU_LINUX_FS_H
#define ZHAOXU_LINUX_FS_H

#define FS_SECRM_FL 0x00000001
#define FS_UNRM_FL 0x00000002
#define FS_COMPR_FL 0x00000004
#define FS_SYNC_FL 0x00000008
#define FS_IMMUTABLE_FL 0x00000010
#define FS_APPEND_FL 0x00000020
#define FS_NODUMP_FL 0x00000040
#define FS_NOATIME_FL 0x00000080
#define FS_DIRTY_FL 0x00000100
#define FS_COMPRBLK_FL 0x00000200
#define FS_NOCOMP_FL 0x00000400
#define FS_INDEX_FL 0x00001000
#define FS_JOURNAL_DATA_FL 0x00004000
#define FS_NOTAIL_FL 0x00008000
#define FS_DIRSYNC_FL 0x00010000
#define FS_TOPDIR_FL 0x00020000
#define FS_EXTENT_FL 0x00080000
#define FS_NOCOW_FL 0x00800000
#define FS_PROJINHERIT_FL 0x20000000
#define FS_ENCRYPT_FL 0x00000800
#define FS_INLINE_DATA_FL 0x10000000

struct fsxattr {
    unsigned int fsx_xflags;
    unsigned int fsx_extsize;
    unsigned int fsx_nextents;
    unsigned int fsx_projid;
    unsigned int fsx_cowextsize;
    unsigned char fsx_pad[8];
};

#define FS_IOC_FSGETXATTR 0x801c581f
#define FS_IOC_FSSETXATTR 0x401c5820
#define FS_IOC_GETVERSION 0x80087601
#define FS_IOC_SETVERSION 0x40087602

#define FIFREEZE 0xc0045877
#define FITHAW 0xc0045878

#define FS_IOC_GETFLAGS 0x80086601
#define FS_IOC_SETFLAGS 0x40086602

#define BLKGETSIZE 0x1260
#define BLKRAGET 0x1263
#define BLKRASET 0x1262
#define BLKROGET 0x125e
#define BLKFLSBUF 0x1261
#define BLKBSZSET 0x40041271
#define BLKZEROOUT 0x127f
#define BLKGETSIZE64 0x80081272
#define BLKSSZGET 0x1268
#define BLKBSZGET 0x80041270
#define BLKDISCARD 0x1277
#define BLKSECDISCARD 0x127d

#endif
EOF

mkdir -p "$port/scsi"
cat > "$port/netinet/ip_icmp.h" <<'EOF'
/* ICMP echo structures for ping. */
#ifndef ZHAOXU_NETINET_IP_ICMP_H
#define ZHAOXU_NETINET_IP_ICMP_H

#include <stdint.h>

struct icmphdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
        uint32_t gateway;
    } un;
};

#define ICMP_ECHOREPLY 0
#define ICMP_DEST_UNREACH 3
#define ICMP_ECHO 8
#define ICMP_TIME_EXCEEDED 11

#endif
EOF

cat > "$port/linux/cdrom.h" <<'EOF'
/* CD-ROM control ioctls for the eject toy. */
#ifndef ZHAOXU_LINUX_CDROM_H
#define ZHAOXU_LINUX_CDROM_H

#define CDROMEJECT 0x5309
#define CDROMCLOSETRAY 0x5319
#define CDROM_LOCKDOOR 0x5329
#define CDROM_DRIVE_STATUS 0x5326
#define CDROM_DISC_STATUS 0x5327
#define CDROM_GET_CAPABILITY 0x5331

#define CDS_NO_INFO 0
#define CDS_NO_DISC 1
#define CDS_TRAY_OPEN 2
#define CDS_DRIVE_NOT_READY 3
#define CDS_DISC_OK 4

#define CDSL_CURRENT 0x7fffffff

#endif
EOF

cat > "$port/scsi/scsi.h" <<'EOF'
/* Generic SCSI constants used by the eject toy. */
#ifndef ZHAOXU_SCSI_SCSI_H
#define ZHAOXU_SCSI_SCSI_H

#define SCSI_IOCTL_SEND_COMMAND 1
#define ALLOW_MEDIUM_REMOVAL 0x1e
#define START_STOP 0x1b
#define TEST_UNIT_READY 0x00

#endif
EOF

cat > "$port/limits.h" <<'EOF'
#ifndef ZHAOXU_LIMITS_OVERLAY_H
#define ZHAOXU_LIMITS_OVERLAY_H

#include_next <limits.h>

#define _POSIX_ARG_MAX 4096
#define _POSIX_CHILD_MAX 25
#define _POSIX_DELAYTIMER_MAX 32
#define _POSIX_HOST_NAME_MAX 255
#define _POSIX_LINK_MAX 8
#define _POSIX_LOGIN_NAME_MAX 9
#define _POSIX_MAX_CANON 255
#define _POSIX_MAX_INPUT 255
#define _POSIX_NAME_MAX 14
#define _POSIX_NGROUPS_MAX 8
#define _POSIX_OPEN_MAX 20
#define _POSIX_PATH_MAX 256
#define _POSIX_PIPE_BUF 512
#define _POSIX_RE_DUP_MAX 255
#define _POSIX_SEM_NSEMS_MAX 256
#define _POSIX_SEM_VALUE_MAX 32767
#define _POSIX_SIGQUEUE_MAX 32
#define _POSIX_SSIZE_MAX 32767
#define _POSIX_STREAM_MAX 8
#define _POSIX_SYMLINK_MAX 255
#define _POSIX_SYMLOOP_MAX 8
#define _POSIX_TIMER_MAX 32
#define _POSIX_TTY_NAME_MAX 9
#define _POSIX_TZNAME_MAX 6
#define _POSIX_RTSIG_MAX 8
#define _POSIX_THREAD_DESTRUCTOR_ITERATIONS 4
#define _POSIX_THREAD_KEYS_MAX 128
#define _POSIX_THREAD_THREADS_MAX 64

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(((size_t)-1) >> 1))
#endif
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#define _POSIX_CLOCKRES_MIN 20000000
#define _POSIX_MONOTONIC_CLOCK 200809L
#define _POSIX_SEMAPHORES (-1)
#define _POSIX_THREADS 200809L
#define _POSIX_SHELL 1
#define _POSIX_SPAWN (-1)
#define _POSIX_SPORADIC_SERVER (-1)
#define _POSIX_SYNC_IO (-1)
#define _POSIX_THREAD_ATTR_STACKADDR 200809L
#define _POSIX_THREAD_ATTR_STACKSIZE 200809L
#define _POSIX_THREAD_CPUTIME 200809L
#define _POSIX_THREAD_PRIO_INHERIT (-1)
#define _POSIX_THREAD_PRIO_PROTECT (-1)
#define _POSIX_THREAD_PRIORITY_SCHEDULING (-1)
#define _POSIX_THREAD_PROCESS_SHARED 200809L
#define _POSIX_THREAD_SAFE_FUNCTIONS 200809L
#define _POSIX_TRACE (-1)
#define _POSIX_TRACE_EVENT_FILTER (-1)
#define _POSIX_TRACE_INHERIT (-1)
#define _POSIX_TRACE_LOG (-1)
#define _POSIX_TYPED_MEMORY_OBJECTS (-1)

#define _POSIX2_BC_BASE_MAX 99
#define _POSIX2_BC_DIM_MAX 2048
#define _POSIX2_BC_SCALE_MAX 99
#define _POSIX2_BC_STRING_MAX 1000
#define _POSIX2_CHARCLASS_NAME_MAX 14
#define _POSIX2_COLL_WEIGHTS_MAX 2
#define _POSIX2_EXPR_NEST_MAX 32
#define _POSIX2_LINE_MAX 2048
#define _POSIX2_RE_DUP_MAX 255
#define _POSIX2_VERSION 200809L

#define _POSIX_AIO_LISTIO_MAX 2
#define _POSIX_AIO_MAX 1
#define _POSIX_VDISABLE 0

#define _POSIX_VERSION 200809L
#define _POSIX_JOB_CONTROL 1
#define _POSIX_SAVED_IDS 1
#define _POSIX_CLOCK_SELECTION 200809L
#define _POSIX_CPUTIME 200809L
#define _POSIX_MAPPED_FILES 200809L
#define _POSIX_MEMLOCK 200809L
#define _POSIX_MEMLOCK_RANGE 200809L
#define _POSIX_MEMORY_PROTECTION 200809L
#define _POSIX_MESSAGE_PASSING 200809L
#define _POSIX_PRIORITIZED_IO (-1)
#define _POSIX_PRIORITY_SCHEDULING (-1)
#define _POSIX_READER_WRITER_LOCKS 200809L
#define _POSIX_REALTIME_SIGNALS 200809L
#define _POSIX_SHARED_MEMORY_OBJECTS 200809L
#define _POSIX_SYNCHRONIZED_IO (-1)
#define _POSIX_TIMEOUTS 200809L
#define _POSIX_ADVISORY_INFO (-1)
#define _POSIX_BARRIERS 200809L
#define _POSIX_RAW_SOCKETS 200809L
#define _POSIX_IPV6 200809L
#define _XOPEN_VERSION 700
#define _XOPEN_UNIX 1
#define _XOPEN_CRYPT 1
#define _XOPEN_ENH_I18N 1
#define _XOPEN_SHM 1
#define _XOPEN_UUCP (-1)

#endif
EOF

cat > "$port/utmpx.h" <<'EOF'
/* POSIX utmpx record layout; newlib ships the API without the fields. */
#ifndef ZHAOXU_UTMPX_OVERLAY_H
#define ZHAOXU_UTMPX_OVERLAY_H

#include <sys/types.h>
#include <sys/time.h>

#define EMPTY 0
#define RUN_LVL 1
#define BOOT_TIME 2
#define NEW_TIME 3
#define OLD_TIME 4
#define INIT_PROCESS 5
#define LOGIN_PROCESS 6
#define USER_PROCESS 7
#define DEAD_PROCESS 8

struct exit_status {
    short e_termination;
    short e_exit;
};

struct utmpx {
    short ut_type;
    pid_t ut_pid;
    char ut_line[32];
    char ut_id[4];
    char ut_user[32];
    char ut_host[256];
    struct exit_status ut_exit;
    long ut_session;
    struct timeval ut_tv;
    int32_t ut_addr_v6[4];
    char zx_unused[20];
};

struct utmpx *getutxent(void);
void setutxent(void);
void endutxent(void);

#endif
EOF

cat > "$port/signal.h" <<'EOF'
/* POSIX signal surface with the complete siginfo_t layout toybox expects.
 * newlib ships a minimal siginfo_t; this port supplies the real one. */
#ifndef ZHAOXU_SIGNAL_H
#define ZHAOXU_SIGNAL_H

#include <sys/types.h>

typedef unsigned long sigset_t;
typedef void (*sighandler_t)(int);

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPWR 30
#define SIGSYS 31
#define NSIG 65

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

union sigval {
    int sival_int;
    void *sival_ptr;
};

typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    union {
        struct {
            pid_t si_pid;
            uid_t si_uid;
            int si_status;
            long si_utime;
            long si_stime;
        } _sigchld;
        struct {
            pid_t si_pid;
            uid_t si_uid;
        } _kill;
        struct {
            int si_int;
            void *si_ptr;
        } _rt;
        void *si_addr;
    } _sifields;
} siginfo_t;

#define si_pid _sifields._sigchld.si_pid
#define si_uid _sifields._sigchld.si_uid
#define si_status _sifields._sigchld.si_status
#define si_addr _sifields.si_addr

#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
#define CLD_TRAPPED 4
#define CLD_STOPPED 5
#define CLD_CONTINUED 6

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO 0x00000004
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

struct sigaction {
    union {
        sighandler_t sa_handler;
        void (*sa_sigaction)(int, siginfo_t *, void *);
    } __zx_handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#define sa_handler __zx_handler.sa_handler
#define sa_sigaction __zx_handler.sa_sigaction

sighandler_t signal(int number, sighandler_t handler);
int raise(int number);
int kill(pid_t pid, int number);
int killpg(int group, int number);
int sigaction(int number, const struct sigaction *action,
              struct sigaction *old);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int number);
int sigdelset(sigset_t *set, int number);
int sigismember(const sigset_t *set, int number);
int sigprocmask(int how, const sigset_t *set, sigset_t *old);
int sigsuspend(const sigset_t *mask);
int sigpending(sigset_t *set);
int siginterrupt(int number, int flag);
char *strsignal(int number);
void psignal(int number, const char *message);

#endif
EOF

cat > "$port/sys/wait.h" <<'EOF'
#ifndef ZHAOXU_SYS_WAIT_OVERLAY_H
#define ZHAOXU_SYS_WAIT_OVERLAY_H

#include_next <sys/wait.h>
#include <sys/resource.h>

pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage);

#endif
EOF

cat > "$port/sys/reboot.h" <<'EOF'
/* System reset interface; the kernel reboots through SYS_REBOOT. */
#ifndef ZHAOXU_SYS_REBOOT_H
#define ZHAOXU_SYS_REBOOT_H

#define RB_AUTOBOOT 0x01234567
#define RB_HALT_SYSTEM 0xcdef0123
#define RB_POWER_OFF 0x4321fedc

int reboot(int magic);

#endif
EOF

cat > "$port/grp.h" <<'EOF'
/* Group database lookups. */
#ifndef ZHAOXU_GRP_H
#define ZHAOXU_GRP_H

#include <sys/types.h>

struct group {
    char *gr_name;
    char *gr_passwd;
    gid_t gr_gid;
    char **gr_mem;
};

struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);
int getgrouplist(const char *user, gid_t group, gid_t *groups, int *count);
int getgrnam_r(const char *name, struct group *result, char *buffer,
               size_t size, struct group **found);
int getgrgid_r(gid_t gid, struct group *result, char *buffer, size_t size,
               struct group **found);
int initgroups(const char *user, gid_t group);

#endif
EOF

cat > "$port/shadow.h" <<'EOF'
/* Shadow password entries for the login toys. */
#ifndef ZHAOXU_SHADOW_H
#define ZHAOXU_SHADOW_H

struct spwd {
    char *sp_namp;
    char *sp_pwdp;
    long sp_lstchg;
    long sp_min;
    long sp_max;
    long sp_warn;
    long sp_inact;
    long sp_expire;
    unsigned long sp_flag;
};

struct spwd *getspnam(const char *name);

#endif
EOF

cat > "$port/linux/soundcard.h" <<'EOF'
/* OSS mixer control surface for the mix toy. */
#ifndef ZHAOXU_LINUX_SOUNDCARD_H
#define ZHAOXU_LINUX_SOUNDCARD_H

#define SOUND_MIXER_VOLUME 0
#define SOUND_MIXER_BASS 1
#define SOUND_MIXER_TREBLE 2
#define SOUND_MIXER_PCM 4
#define SOUND_MIXER_SPEAKER 5
#define SOUND_MIXER_LINE 6
#define SOUND_MIXER_MIC 7
#define SOUND_MIXER_CD 8
#define SOUND_MIXER_IMIX 9
#define SOUND_MIXER_ALTPCM 10
#define SOUND_MIXER_RECLEV 11
#define SOUND_MIXER_IGAIN 12
#define SOUND_MIXER_OGAIN 13

#define SOUND_MIXER_NRDEVICES 14

#define SOUND_DEVICE_NAMES {"vol", "bass", "treble", "synth", "pcm", \
    "speaker", "line", "mic", "cd", "imix", "altpcm", "reclev", "igain", \
    "ogain"}
#define MIXER_READ(dev) (0x80044d00 | (dev))
#define MIXER_WRITE(dev) (0xc0044d00 | (dev))

#define SOUND_MIXER_READ(dev) (0x80044d00 | (dev))
#define SOUND_MIXER_WRITE(dev) (0xc0044d00 | (dev))
#define SOUND_MIXER_READ_DEVMASK 0x80044dfe
#define SOUND_MIXER_READ_RECMASK 0x80044dfd
#define SOUND_MIXER_READ_STEREODEVS 0x80044dfb

#endif
EOF

cat > "$port/linux/watchdog.h" <<'EOF'
/* Watchdog timer control surface. */
#ifndef ZHAOXU_LINUX_WATCHDOG_H
#define ZHAOXU_LINUX_WATCHDOG_H

#define WDIOC_GETSUPPORT 0x80285700
#define WDIOC_GETSTATUS 0x80045701
#define WDIOC_GETBOOTSTATUS 0x80045702
#define WDIOC_SETOPTIONS 0x80045704
#define WDIOC_KEEPALIVE 0x80045705
#define WDIOC_SETTIMEOUT 0xc0045706
#define WDIOC_GETTIMEOUT 0x80045707

#define WDIOS_DISABLECARD 0x0001
#define WDIOS_ENABLECARD 0x0002

#endif
EOF

cat > "$port/linux/sockios.h" <<'EOF'
/* Socket-level ioctls beyond the interface set. */
#ifndef ZHAOXU_LINUX_SOCKIOS_H
#define ZHAOXU_LINUX_SOCKIOS_H

#define SIOCGIFVLAN 0x8982
#define SIOCSIFVLAN 0x8983
#define ADD_VLAN_CMD 0
#define DEL_VLAN_CMD 1
#define SET_VLAN_INGRESS_PRIORITY_CMD 2
#define SET_VLAN_EGRESS_PRIORITY_CMD 3
#define SET_VLAN_FLAG_CMD 4
#define GET_VLAN_REALDEV_NAME_CMD 5
#define GET_VLAN_VID_CMD 6
#define SET_VLAN_NAME_TYPE_CMD 7

struct vlan_ioctl_args {
    int cmd;
    char device1[24];
    union {
        char device2[24];
        int VID;
        unsigned int skb_priority;
        unsigned int name_type;
        unsigned int bind_type;
        unsigned short flag;
    } u;
    short vlan_qos;
};

#define SIOCBRADDBR 0x89a0
#define SIOCBRDELBR 0x89a1
#define SIOCBRADDIF 0x89a2
#define SIOCBRDELIF 0x89a3
#define SIOCETHTOOL 0x8946

#endif
EOF

cat > "$port/linux/if_vlan.h" <<'EOF'
/* VLAN interface control for vconfig. */
#ifndef ZHAOXU_LINUX_IF_VLAN_H
#define ZHAOXU_LINUX_IF_VLAN_H

#define VLAN_NAME_TYPE_PLUS_VID 0
#define VLAN_NAME_TYPE_RAW_PLUS_VID 1
#define VLAN_NAME_TYPE_PLUS_VID_NO_PAD 2
#define VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD 3

#endif
EOF

cat > "$port/linux/kd.h" <<'EOF'
/* Keyboard mode control for openvt. */
#ifndef ZHAOXU_LINUX_KD_H
#define ZHAOXU_LINUX_KD_H

#define KDGKBMODE 0x4b44
#define KDSKBMODE 0x4b45
#define KDGKBTYPE 0x4b33

#define K_RAW 0x00
#define K_XLATE 0x01
#define K_MEDIUMRAW 0x02
#define K_UNICODE 0x03

#endif
EOF

cat > "$port/linux/vt.h" <<'EOF'
/* Virtual terminal control for openvt. */
#ifndef ZHAOXU_LINUX_VT_H
#define ZHAOXU_LINUX_VT_H

#define VT_OPENQRY 0x5600
#define VT_GETSTATE 0x5603
#define VT_ACTIVATE 0x5606
#define VT_DISALLOCATE 0x5608
#define VT_WAITACTIVE 0x5607

struct vt_stat {
    unsigned short v_active;
    unsigned short v_signal;
    unsigned short v_state;
};

#endif
EOF

cat > "$port/linux/sched.h" <<'EOF'
/* Namespace clone flags for nsenter. */
#ifndef ZHAOXU_LINUX_SCHED_H
#define ZHAOXU_LINUX_SCHED_H

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_NEWNS 0x00020000
#define CLONE_NEWCGROUP 0x02000000
#define CLONE_NEWUTS 0x04000000
#define CLONE_NEWIPC 0x08000000
#define CLONE_NEWUSER 0x10000000
#define CLONE_NEWPID 0x20000000
#define CLONE_NEWNET 0x40000000

#endif
EOF

cat > "$port/linux/i2c-dev.h" <<'EOF'
/* Character-device I2C interface, same ioctls as linux/i2c.h. */
#ifndef ZHAOXU_LINUX_I2C_DEV_H
#define ZHAOXU_LINUX_I2C_DEV_H
#include <linux/i2c.h>
#endif
EOF

cat > "$port/linux/gpio.h" <<'EOF'
/* GPIO character-device control for gpiod. */
#ifndef ZHAOXU_LINUX_GPIO_H
#define ZHAOXU_LINUX_GPIO_H

#include <stdint.h>

struct gpio_v2_line_request {
    uint32_t offsets[64];
    char consumer[32];
    uint32_t config_flags;
    uint32_t num_lines;
    int32_t fd;
};

struct gpiochip_info {
    char name[32];
    char label[32];
    uint32_t lines;
};

struct gpioline_info {
    uint32_t line_offset;
    uint32_t flags;
    char name[32];
    char consumer[32];
};

struct gpiohandle_data {
    uint8_t values[64];
};

struct gpiohandle_request {
    uint32_t lineoffsets[64];
    uint32_t flags;
    uint8_t default_values[64];
    char consumer_label[32];
    uint32_t lines;
    int fd;
};

#define GPIOHANDLES_MAX 64
#define GPIO_GET_LINEHANDLE_IOCTL 0xc250b40b
#define GPIOHANDLE_REQUEST_INPUT 0x00000001
#define GPIOHANDLE_REQUEST_OUTPUT 0x00000002
#define GPIOHANDLE_REQUEST_ACTIVE_LOW 0x00000004
#define GPIOHANDLE_GET_LINE_VALUES_IOCTL 0xc040b408
#define GPIOHANDLE_SET_LINE_VALUES_IOCTL 0xc040b409

#define GPIOLINE_FLAG_IS_OUT 0x0002
#define GPIOLINE_FLAG_ACTIVE_LOW 0x0004
#define GPIOLINE_FLAG_KERNEL 0x0008

#define GPIO_GET_CHIPINFO_IOCTL 0x8044b401
#define GPIO_GET_LINEINFO_IOCTL 0xc148b402
#define GPIO_GET_LINEINFO_V2_IOCTL 0xc148b426
#define GPIO_V2_GET_LINE_IOCTL 0xc250b407
#define GPIO_V2_LINE_GET_VALUES_IOCTL 0xc040b40e
#define GPIO_V2_LINE_SET_VALUES_IOCTL 0xc040b40f

#endif
EOF

cat > "$port/linux/i2c.h" <<'EOF'
/* I2C device control surface for i2cdetect/i2cget. */
#ifndef ZHAOXU_LINUX_I2C_H
#define ZHAOXU_LINUX_I2C_H

#include <stdint.h>

struct i2c_msg {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint8_t *buf;
};

struct i2c_rdwr_ioctl_data {
    struct i2c_msg *msgs;
    uint32_t nmsgs;
};

union i2c_smbus_data {
    uint8_t byte;
    uint16_t word;
    uint8_t block[32];
};

struct i2c_smbus_ioctl_data {
    uint8_t read_write;
    uint8_t command;
    uint32_t size;
    union i2c_smbus_data *data;
};

#define I2C_SMBUS_READ 1
#define I2C_SMBUS_WRITE 0
#define I2C_SMBUS_QUICK 0
#define I2C_SMBUS_BYTE 1
#define I2C_SMBUS_BYTE_DATA 2
#define I2C_SMBUS_BLOCK_MAX 32
#define I2C_M_RD 0x0001
#define I2C_SMBUS_WORD_DATA 3
#define I2C_SMBUS_I2C_BLOCK_DATA 8

#define I2C_SMBUS 0x0720

#define I2C_RDWR 0x0707
#define I2C_RDWR_IOCTL_MAX_MSGS 42
#define I2C_SLAVE 0x0703
#define I2C_SLAVE_FORCE 0x0706
#define I2C_TENBIT 0x0704
#define I2C_FUNCS 0x0705

#define I2C_FUNC_I2C 0x00000001
#define I2C_FUNC_SMBUS_READ_BYTE_DATA 0x00080000
#define I2C_FUNC_SMBUS_WRITE_BYTE_DATA 0x00100000
#define I2C_FUNC_SMBUS_READ_BYTE 0x00020000
#define I2C_FUNC_SMBUS_WRITE_BYTE 0x00040000

#endif
EOF

cat > "$port/linux/nbd.h" <<'EOF'
/* Network block device control interface. */
#ifndef ZHAOXU_LINUX_NBD_H
#define ZHAOXU_LINUX_NBD_H

#include <stdint.h>

#define NBD_SET_SOCK 0xab00
#define NBD_SET_BLKSIZE 0xab01
#define NBD_SET_SIZE 0xab02
#define NBD_SET_SIZE_BLOCKS 0xab07
#define NBD_DO_IT 0xab03
#define NBD_CLEAR_SOCK 0xab04
#define NBD_CLEAR_QUE 0xab05
#define NBD_PRINT_DEBUG 0xab06
#define NBD_DISCONNECT 0xab08
#define NBD_SET_TIMEOUT 0xab09
#define NBD_SET_FLAGS 0xab0a

#define NBD_CMD_READ 0
#define NBD_CMD_WRITE 1
#define NBD_CMD_DISC 2
#define NBD_CMD_FLUSH 3
#define NBD_CMD_TRIM 4

struct nbd_request {
    uint32_t magic;
    uint32_t type;
    char handle[8];
    uint64_t from;
    uint32_t len;
} __attribute__((packed));

struct nbd_reply {
    uint32_t magic;
    uint32_t error;
    char handle[8];
} __attribute__((packed));

#endif
EOF

cat > "$port/linux/rtc.h" <<'EOF'
/* RTC control interface for hwclock. */
#ifndef ZHAOXU_LINUX_RTC_H
#define ZHAOXU_LINUX_RTC_H

#include <stdint.h>

struct rtc_time {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

struct rtc_wkalrm {
    unsigned char enabled;
    unsigned char pending;
    struct rtc_time time;
};

#define RTC_AIE_ON 0x7001
#define RTC_AIE_OFF 0x7002
#define RTC_AF 5

#define RTC_WKALM_RD 0x80287010
#define RTC_WKALM_SET 0x4028700f

#define RTC_SET_TIME 0x4024700a
#define RTC_RD_TIME 0x80247009
#define RTC_UIE_ON 0x40047003
#define RTC_UIE_OFF 0x40047004

#endif
EOF

cat > "$port/linux/loop.h" <<'EOF'
/* Loop device control interface for losetup. */
#ifndef ZHAOXU_LINUX_LOOP_H
#define ZHAOXU_LINUX_LOOP_H

struct loop_info64 {
    unsigned long long lo_device;
    unsigned long long lo_inode;
    unsigned long long lo_rdevice;
    unsigned long long lo_offset;
    unsigned long long lo_sizelimit;
    unsigned int lo_number;
    unsigned int lo_encrypt_type;
    unsigned int lo_flags;
    unsigned char lo_file_name[64];
    unsigned char lo_crypt_name[64];
    unsigned char lo_encrypt_key[32];
    unsigned long long lo_init[2];
};

#define LO_NAME_SIZE 64
#define LO_KEY_SIZE 32

#define LOOP_SET_FD 0x4c00
#define LOOP_CLR_FD 0x4c01
#define LOOP_SET_STATUS 0x4c02
#define LOOP_GET_STATUS 0x4c03
#define LOOP_SET_STATUS64 0x4c04
#define LOOP_GET_STATUS64 0x4c05
#define LOOP_SET_CAPACITY 0x4c07
#define LOOP_CTL_GET_FREE 0x4c82

#endif
EOF

cat > "$port/linux/if_tun.h" <<'EOF'
/* TUN/TAP interface control; declarations for the tunctl toy. */
#ifndef ZHAOXU_LINUX_IF_TUN_H
#define ZHAOXU_LINUX_IF_TUN_H

#define TUNSETIFF 0x400454ca
#define TUNSETPERSIST 0x400454cb
#define TUNSETOWNER 0x400454cc
#define TUNSETGROUP 0x400454ce
#define IFF_TUN 0x0001
#define IFF_TAP 0x0002
#define IFF_NO_PI 0x1000

#endif
EOF

cat > "$port/scsi/sg.h" <<'EOF'
/* SCSI generic device surface; declarations for the eject toy. */
#ifndef ZHAOXU_SCSI_SG_H
#define ZHAOXU_SCSI_SG_H

#define SG_GET_VERSION_NUM 0x2282
#define SG_DXFER_NONE (-1)
#define SG_DXFER_TO_DEV (-2)
#define SG_DXFER_FROM_DEV (-3)
#define SG_DXFER_TO_FROM_DEV (-4)

struct sg_io_hdr {
    int interface_id;
    int dxfer_direction;
    unsigned char cmd_len;
    unsigned char mx_sb_len;
    unsigned short iovec_count;
    unsigned int dxfer_len;
    void *dxferp;
    unsigned char *cmdp;
    void *sbp;
    unsigned int timeout;
    unsigned int flags;
    int pack_id;
    void *usr_ptr;
    unsigned char status;
    unsigned char masked_status;
    unsigned char msg_status;
    unsigned char sb_len_wr;
    unsigned short host_status;
    unsigned short driver_status;
    int resid;
    unsigned int duration;
    unsigned int info;
};

typedef struct sg_io_hdr sg_io_hdr_t;

#define SCSI_IOCTL_GET_IDLUN 0x5382
#define SCSI_IOCTL_GET_BUS_NUMBER 0x5386
#define SG_IO 0x2285
#define SG_DXFER_NONE (-1)

#endif
EOF

cat > "$port/fcntl.h" <<'EOF'
#ifndef ZHAOXU_FCNTL_OVERLAY_H
#define ZHAOXU_FCNTL_OVERLAY_H

#include_next <fcntl.h>

#ifndef O_NDELAY
#define O_NDELAY O_NONBLOCK
#endif

#endif
EOF

cat > "$port/ifaddrs.h" <<'EOF'
/* Interface address enumeration, served by the BSD network subsystem. */
#ifndef ZHAOXU_IFADDRS_H
#define ZHAOXU_IFADDRS_H

#include <sys/socket.h>

struct ifaddrs {
    struct ifaddrs *ifa_next;
    char *ifa_name;
    unsigned int ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    union {
        struct sockaddr *ifu_broadaddr;
        struct sockaddr *ifu_dstaddr;
    } ifa_ifu;
    void *ifa_data;
};

#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr ifa_ifu.ifu_dstaddr

int getifaddrs(struct ifaddrs **addresses);
void freeifaddrs(struct ifaddrs *addresses);

#endif
EOF

cat > "$port/errno.h" <<'EOF'
#ifndef ZHAOXU_ERRNO_OVERLAY_H
#define ZHAOXU_ERRNO_OVERLAY_H

#include_next <errno.h>

#ifndef ENOTBLK
#define ENOTBLK 15
#endif
#ifndef ENOMEDIUM
#define ENOMEDIUM 123
#endif
#ifndef EBADR
#define EBADR 53
#endif

#endif
EOF

cat > "$port/unistd.h" <<'EOF'
#ifndef ZHAOXU_UNISTD_OVERLAY_H
#define ZHAOXU_UNISTD_OVERLAY_H

#include_next <unistd.h>

char *crypt(const char *key, const char *salt);

#define SWAP_FLAG_PREFER 0x8000
#define SWAP_FLAG_PRIO_SHIFT 0

int swapon(const char *path, int flags);
int swapoff(const char *path);

#ifndef UTIME_NOW
#define UTIME_NOW (-2L)
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT (-1L)
#endif

#define _SC_ARG_MAX 0
#define _SC_CHILD_MAX 1
#define _SC_CLK_TCK 2
#define _SC_NGROUPS_MAX 3
#define _SC_OPEN_MAX 4
#define _SC_STREAM_MAX 5
#define _SC_TZNAME_MAX 6
#define _SC_JOB_CONTROL 7
#define _SC_SAVED_IDS 8
#define _SC_REALTIME_SIGNALS 9
#define _SC_PRIORITY_SCHEDULING 10
#define _SC_TIMERS 11
#define _SC_ASYNCHRONOUS_IO 12
#define _SC_PRIORITIZED_IO 13
#define _SC_SYNCHRONIZED_IO 14
#define _SC_FSYNC 15
#define _SC_MAPPED_FILES 16
#define _SC_MEMLOCK 17
#define _SC_MEMLOCK_RANGE 18
#define _SC_MEMORY_PROTECTION 19
#define _SC_MESSAGE_PASSING 20
#define _SC_SEMAPHORES 21
#define _SC_SHARED_MEMORY_OBJECTS 22
#define _SC_AIO_LISTIO_MAX 23
#define _SC_AIO_MAX 24
#define _SC_AIO_PRIO_DELTA_MAX 25
#define _SC_DELAYTIMER_MAX 26
#define _SC_MQ_OPEN_MAX 27
#define _SC_MQ_PRIO_MAX 28
#define _SC_VERSION 29
#define _SC_PAGESIZE 30
#define _SC_RTSIG_MAX 31
#define _SC_SEM_NSEMS_MAX 32
#define _SC_SEM_VALUE_MAX 33
#define _SC_SIGQUEUE_MAX 34
#define _SC_TIMER_MAX 35
#ifndef _SC_TRACE
#define _SC_TRACE 36
#endif
#define _SC_TRACE_EVENT_FILTER 37
#define _SC_TRACE_INHERIT 38
#define _SC_TRACE_LOG 39
#define _SC_THREADS 40
#define _SC_THREAD_ATTR_STACKADDR 41
#define _SC_THREAD_ATTR_STACKSIZE 42
#define _SC_THREAD_CPUTIME 43
#define _SC_THREAD_DESTRUCTOR_ITERATIONS 44
#define _SC_THREAD_KEYS_MAX 45
#define _SC_THREAD_PRIO_INHERIT 46
#define _SC_THREAD_PRIO_PROTECT 47
#define _SC_THREAD_PRIORITY_SCHEDULING 48
#define _SC_THREAD_PROCESS_SHARED 49
#define _SC_THREAD_SAFE_FUNCTIONS 50
#define _SC_THREAD_SPORADIC_SERVER 51
#define _SC_THREAD_THREADS_MAX 52
#define _SC_THREAD_ROBUST_PRIO_INHERIT 53
#define _SC_THREAD_ROBUST_PRIO_PROTECT 54
#define _SC_THREAD_STACK_MIN 55
#define _SC_UIO_MAXIOV 60
#define _SC_AVPHYS_PAGES 80
#define _SC_PHYS_PAGES 81
#define _SC_NPROCESSORS_CONF 82
#define _SC_NPROCESSORS_ONLN 83
#define _SC_LEVEL1_ICACHE_SIZE 84
#define _SC_V7_ILP32_OFF32 87
#define _SC_V7_ILP32_OFFBIG 88
#define _SC_V7_LP64_OFF64 89
#define _SC_V7_LPBIG_OFFBIG 90
#define _SC_XOPEN_UUCP 91

#define _CS_PATH 0
#define _CS_V7_ENV 1
#define _PC_VDISABLE 5

/* FreeBSD crypt_r state, declared by <unistd.h> on that system. */
struct crypt_data {
    int initialized;
    char __buf[256];
};
char *crypt_r(const char *passwd, const char *salt,
              struct crypt_data *data);
const char *crypt_get_format(void);
int crypt_set_format(const char *format);

#endif
EOF

cat > "$port/sys/sysmacros.h" <<'EOF'
#ifndef ZHAOXU_SYS_SYSMACROS_H
#define ZHAOXU_SYS_SYSMACROS_H

#include <sys/types.h>

static inline unsigned int zx_major(dev_t dev)
{
    return (unsigned int)((dev >> 8) & 0xfff);
}

static inline unsigned int zx_minor(dev_t dev)
{
    return (unsigned int)((dev & 0xff) | ((dev >> 12) & 0xfff00));
}

static inline dev_t zx_makedev(unsigned int major, unsigned int minor)
{
    return (dev_t)((minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12));
}

#define major(dev) zx_major(dev)
#define minor(dev) zx_minor(dev)
#define makedev(maj, min) zx_makedev(maj, min)

#endif
EOF

cat > "$port/sys/stat.h" <<'EOF'
#ifndef ZHAOXU_SYS_STAT_OVERLAY_H
#define ZHAOXU_SYS_STAT_OVERLAY_H

#include_next <sys/stat.h>

int mknod(const char *path, mode_t mode, dev_t dev);
int lstat(const char *path, struct stat *buffer);

#endif
EOF

cat > "$port/crypt.h" <<'EOF'
/* Password hashing entry point; the implementation is a separate port. */
#ifndef ZHAOXU_CRYPT_H
#define ZHAOXU_CRYPT_H

char *crypt(const char *key, const char *salt);

#endif
EOF

cat > "$port/mntent.h" <<'EOF'
/* Mount-table surface for building toybox; mount is trimmed until the kernel
 * has a mount/statfs implementation. */
#ifndef ZHAOXU_MNTENT_H
#define ZHAOXU_MNTENT_H

#include <stdio.h>

struct mntent {
    char *mnt_fsname;
    char *mnt_dir;
    char *mnt_type;
    char *mnt_opts;
    int mnt_freq;
    int mnt_passno;
};

FILE *setmntent(const char *name, const char *mode);
struct mntent *getmntent(FILE *stream);
int endmntent(FILE *stream);

#endif
EOF

cat > "$port/byteswap.h" <<'EOF'
/* glibc byte-swap helpers newlib does not provide. */
#ifndef ZHAOXU_BYTESWAP_H
#define ZHAOXU_BYTESWAP_H

#include <stdint.h>

static inline uint16_t bswap_16(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

static inline uint32_t bswap_32(uint32_t value)
{
    return __builtin_bswap32(value);
}

static inline uint64_t bswap_64(uint64_t value)
{
    return __builtin_bswap64(value);
}

#endif
EOF

cat > "$port/sys/mount.h" <<'EOF'
/* mount/statfs surface: the kernel keeps no user-visible mount list and
 * exports no mount syscall, so the calls fail with ENOSYS. */
#ifndef ZHAOXU_SYS_MOUNT_H
#define ZHAOXU_SYS_MOUNT_H

#include <sys/types.h>

struct statfs {
    unsigned long f_type;
    unsigned long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_fsid;
    unsigned long f_namelen;
    unsigned long f_frsize;
    unsigned long f_flags;
    unsigned long f_spare[4];
    char f_fstypename[16];
    char f_mntonname[80];
    char f_mntfromname[80];
};

#define MNT_WAIT 1
#define MNT_NOWAIT 2

#define MS_RDONLY 0x0001
#define MS_NOSUID 0x0002
#define MS_NODEV 0x0004
#define MS_NOEXEC 0x0008
#define MS_REMOUNT 0x0020
#define MS_BIND 0x1000
#define MS_REC 0x4000
#define MS_REC 0x4000
#define MS_NOATIME 0x0400
#define MS_NODIRATIME 0x0800
#define MS_SILENT 0x8000
#define MS_RELATIME 0x200000
#define MS_SYNCHRONOUS 0x0010
#define MS_SHARED 0x100000
#define MS_PRIVATE 0x40000
#define MS_SLAVE 0x80000
#define MS_MOVE 0x2000
#define MS_UNBINDABLE 0x20000

#define MNT_RDONLY 0x00000001
#define MNT_SYNCHRONOUS 0x00000002
#define MNT_NOEXEC 0x00000004
#define MNT_NOSUID 0x00000008

#define MNT_FORCE 0x00000001
#define MNT_DETACH 0x00000002

int statfs(const char *path, struct statfs *buffer);
int fstatfs(int fd, struct statfs *buffer);
int mount(const char *source, const char *target, const char *type,
          unsigned long flags, const void *data);
int umount(const char *target);
int umount2(const char *target, int flags);
int getmntinfo(struct statfs **buffer, int flags);

#endif
EOF

cat > "$port/sys/termios.h" <<'EOF'
/* Platform termios constants this newlib build does not ship. */
#ifndef ZHAOXU_SYS_TERMIOS_H
#define ZHAOXU_SYS_TERMIOS_H

#define NCCS 19

#define IGNBRK 0x0001
#define BRKINT 0x0002
#define PARMRK 0x0008
#define ISTRIP 0x0020
#define INLCR 0x0040
#define IGNCR 0x0080
#define ICRNL 0x0100
#define IXON 0x0400
#define IXANY 0x0800
#define IUTF8 0x4000

#define OPOST 0x0001
#define ONLCR 0x0004

#define CSIZE 0x0030
#define CS8 0x0030
#define PARENB 0x0100
#define CREAD 0x0080
#define CLOCAL 0x0800

#define ISIG 0x0001
#define ICANON 0x0002
#define ECHO 0x0008
#define ECHOE 0x0010
#define ECHOK 0x0020
#define ECHONL 0x0040
#define ECHOKE 0x0800
#define ECHOCTL 0x0200
#define IEXTEN 0x8000

#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int actions, const struct termios *termios_p);
void cfmakeraw(struct termios *termios_p);
int tcflush(int fd, int queue_selector);
int tcdrain(int fd);
int cfsetspeed(struct termios *termios_p, speed_t speed);
int tcsendbreak(int fd, int duration);
speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);

#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#define B0 0
#define B9600 9600
#define B19200 19200
#define B38400 38400

#endif
EOF

cat > "$port/sys/mman.h" <<'EOF'
/* Memory-mapping surface for building toybox; the kernel side exists
 * (memfd/mmap syscalls), libgloss supplies the wrappers at link time. */
#ifndef ZHAOXU_SYS_MMAN_H
#define ZHAOXU_SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)-1)

void *mmap(void *address, size_t length, int protection, int flags,
           int fd, off_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);

#endif
EOF

cat > "$port/pty.h" <<'EOF'
/* POSIX pty API declared for toybox; the implementation lands with the
 * SYS_PTY-backed libgloss layer. */
#ifndef ZHAOXU_PTY_H
#define ZHAOXU_PTY_H

#include <sys/types.h>
#include <termios.h>

struct winsize;

int openpty(int *master, int *slave, char *name,
            const struct termios *termios_p, const struct winsize *winp);
pid_t forkpty(int *master, char *name, const struct termios *termios_p,
              const struct winsize *winp);

#endif
EOF

cat > "$port/paths.h" <<'EOF'
#ifndef ZHAOXU_PATHS_H
#define ZHAOXU_PATHS_H
#define _PATH_DEVNULL "/dev/null"
#define _PATH_BSHELL "/bin/bash"
#define _PATH_TTY "/dev/tty"
#define _PATH_CONSOLE "/dev/console"
#define _PATH_DEFPATH "/usr/bin:/bin"
#endif
EOF

cat > "$port/pwd.h" <<'EOF'
#ifndef ZHAOXU_PWD_H
#define ZHAOXU_PWD_H
#include <sys/types.h>
struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};
struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);
int getpwnam_r(const char *name, struct passwd *result, char *buffer,
               size_t size, struct passwd **found);
int getpwuid_r(uid_t uid, struct passwd *result, char *buffer, size_t size,
               struct passwd **found);
#endif
EOF

cat > "$port/sched.h" <<'EOF'
#ifndef ZHAOXU_SCHED_H
#define ZHAOXU_SCHED_H
#include <sys/types.h>
#ifndef SCHED_OTHER
#define SCHED_OTHER 0
#endif
#ifndef SCHED_FIFO
#define SCHED_FIFO 1
#endif
#define SCHED_RR 2
#define SCHED_BATCH 3
#define SCHED_IDLE 5
#define SCHED_RESET_ON_FORK 0x40000000

int sched_yield(void);
int sched_setscheduler(int pid, int policy,
                       const struct sched_param *parameters);
int sched_getscheduler(int pid);
int sched_setparam(int pid, const struct sched_param *parameters);
int sched_getparam(int pid, struct sched_param *parameters);
int sched_get_priority_max(int policy);
int sched_get_priority_min(int policy);
int sched_setaffinity(int pid, size_t size, const void *mask);
int sched_getaffinity(int pid, size_t size, void *mask);

#endif
EOF

cat > "$port/sys/statvfs.h" <<'EOF'
#ifndef ZHAOXU_SYS_STATVFS_H
#define ZHAOXU_SYS_STATVFS_H
#include <sys/types.h>
struct statvfs {
    unsigned long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail;
    unsigned long f_files, f_ffree, f_favail, f_fsid, f_flag, f_namemax;
};
#define ST_RDONLY 0x1

#include <sys/sysinfo.h>
int statvfs(const char *path, struct statvfs *buffer);
int fstatvfs(int fd, struct statvfs *buffer);
#endif
EOF

cat > "$port/sys/times.h" <<'EOF'
#ifndef ZHAOXU_SYS_TIMES_H
#define ZHAOXU_SYS_TIMES_H
#include <sys/types.h>
struct tms {
    clock_t tms_utime, tms_stime, tms_cutime, tms_cstime;
};
clock_t times(struct tms *buffer);
#endif
EOF

cat > "$port/sys/utsname.h" <<'EOF'
#ifndef ZHAOXU_SYS_UTSNAME_H
#define ZHAOXU_SYS_UTSNAME_H
#define ZX_UTS_LEN 65
struct utsname {
    char sysname[ZX_UTS_LEN];
    char nodename[ZX_UTS_LEN];
    char release[ZX_UTS_LEN];
    char version[ZX_UTS_LEN];
    char machine[ZX_UTS_LEN];
    char domainname[ZX_UTS_LEN];
};
int uname(struct utsname *buffer);
#endif
EOF

cat > "$port/sys/resource.h" <<'EOF'
#ifndef ZHAOXU_SYS_RESOURCE_H
#define ZHAOXU_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/types.h>
typedef unsigned long rlim_t;

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_RSS 5
#define RLIMIT_NPROC 6
#define RLIMIT_NOFILE 7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS 9
#define RLIMIT_LOCKS 10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE 12
#define RLIMIT_NICE 13
#define RLIMIT_RTPRIO 14
#define RLIMIT_RTTIME 15
#define RLIM_INFINITY (~0UL)
int getrlimit(int resource, struct rlimit *limits);
int setrlimit(int resource, const struct rlimit *limits);

#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2

int getpriority(int which, int who);
int setpriority(int which, int who, int value);
#endif
EOF

cat > "$port/poll.h" <<'EOF'
#ifndef ZHAOXU_POLL_H
#define ZHAOXU_POLL_H

typedef unsigned long nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN 0x001
#define POLLPRI 0x002
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020

int poll(struct pollfd *fds, nfds_t count, int timeout);

#endif
EOF

cat > "$port/netinet/tcp.h" <<'EOF'
#ifndef ZHAOXU_NETINET_TCP_H
#define ZHAOXU_NETINET_TCP_H
#define TCP_NODELAY 1
#endif
EOF

cat > "$port/netinet/udp.h" <<'EOF'
#ifndef ZHAOXU_NETINET_UDP_H
#define ZHAOXU_NETINET_UDP_H
#endif
EOF

cat > "$port/net/route.h" <<'EOF'
#ifndef ZHAOXU_NET_ROUTE_H
#define ZHAOXU_NET_ROUTE_H
struct rtentry {
    unsigned long rt_flags;
    unsigned short rt_mtu;
};
#define RTF_UP 0x0001
#define RTF_GATEWAY 0x0002
#define RTF_HOST 0x0004
#define RTF_REJECT 0x0008
#define RTF_DYNAMIC 0x0010
#define RTF_MODIFIED 0x0020
#define RTF_XRESOLVE 0x0040
#define RTF_LLINFO 0x0080
#define RTF_STATIC 0x0100
#define RTF_BLACKHOLE 0x0200
#define RTF_CLONING 0x0400
#define RTF_WASCLONED 0x0800
#define RTF_REINSTATE 0x4000
#endif
EOF

cat > "$port/net/ethernet.h" <<'EOF'
#ifndef ZHAOXU_NET_ETHERNET_H
#define ZHAOXU_NET_ETHERNET_H
#define ETH_ALEN 6
#define ETH_HLEN 14
#endif
EOF

cat > "$port/setjmp.h" <<'EOF'
/* newlib has jmp_buf but no POSIX signal jump type; toybox only needs the
 * type for its signal plumbing. */
#ifndef ZHAOXU_SETJMP_OVERLAY_H
#define ZHAOXU_SETJMP_OVERLAY_H

#include_next <setjmp.h>

typedef jmp_buf sigjmp_buf;

/* No signal-mask restore until the signal path exists; the jump itself is
 * the plain non-local jump. */
#define sigsetjmp(env, save) setjmp(env)
#define siglongjmp(env, value) longjmp(env, value)

#endif
EOF

cat > "$port/sys/ttydefaults.h" <<'EOF'
#ifndef ZHAOXU_SYS_TTYDEFAULTS_H
#define ZHAOXU_SYS_TTYDEFAULTS_H

#define CTRL(x) ((x) & 0x1f)

#define CINTR CTRL('C')
#define CQUIT 0x1c
#define CERASE 0x7f
#define CKILL CTRL('U')
#define CEOF CTRL('D')

#define DEF_INTR CTRL('C')
#define DEF_QUIT 0x1c
#define DEF_ERASE 0x7f
#define DEF_KILL CTRL('U')
#define DEF_EOF CTRL('D')

#endif
EOF

cat > "$port/sys/syscall.h" <<'EOF'
#ifndef ZHAOXU_SYS_SYSCALL_H
#define ZHAOXU_SYS_SYSCALL_H

/* Zhaoxu OS exposes the syscall table through libc stubs, not a raw
 * syscall() ABI; syscall() itself returns ENOSYS. */
long syscall(long number, ...);

#define SYS_gettid 1
#define SYS_tgkill 2
#define SYS_getpid 3
#define SYS_sched_setparam 142
#define SYS_sched_getparam 143
#define SYS_sched_setscheduler 144
#define SYS_sched_getscheduler 145
#define SYS_sched_get_priority_max 146
#define SYS_sched_get_priority_min 147
#define SYS_ioprio_set 251
#define SYS_ioprio_get 252
#define SYS_finit_module 313
#define SYS_init_module 175
#define SYS_delete_module 176
#define SYS_reboot 169
#define SYS_pivot_root 155
#define SYS_unshare 272
#define SYS_setns 308
#define SYS_readahead 187
#define SYS_sched_setaffinity 203
#define SYS_sched_getaffinity 204
#define SYS_sched_setattr 314

#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity SYS_sched_setaffinity
#endif
#ifndef __NR_sched_getaffinity
#define __NR_sched_getaffinity SYS_sched_getaffinity
#endif
#define SYS_sched_getattr 315

#ifndef __NR_sched_setattr
#define __NR_sched_setattr SYS_sched_setattr
#endif
#ifndef __NR_sched_getattr
#define __NR_sched_getattr SYS_sched_getattr
#endif

#ifndef __NR_pivot_root
#define __NR_pivot_root SYS_pivot_root
#endif
#ifndef __NR_unshare
#define __NR_unshare SYS_unshare
#endif
#ifndef __NR_setns
#define __NR_setns SYS_setns
#endif
#ifndef __NR_readahead
#define __NR_readahead SYS_readahead
#endif
#ifndef __NR_delete_module
#define __NR_delete_module SYS_delete_module
#endif

#ifndef __NR_ioprio_set
#define __NR_ioprio_set SYS_ioprio_set
#endif
#ifndef __NR_ioprio_get
#define __NR_ioprio_get SYS_ioprio_get
#endif

#endif
EOF

cat > "$port/sys/inotify.h" <<'EOF'
/* inotify has no kernel support yet; the three calls fail with ENOSYS. */
#ifndef ZHAOXU_SYS_INOTIFY_H
#define ZHAOXU_SYS_INOTIFY_H

#include <sys/types.h>

struct inotify_event {
    int wd;
    unsigned int mask;
    unsigned int cookie;
    unsigned int len;
    char name[];
};

#define IN_ACCESS 0x00000001
#define IN_MODIFY 0x00000002
#define IN_ATTRIB 0x00000004
#define IN_CLOSE_WRITE 0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_OPEN 0x00000020
#define IN_MOVED_FROM 0x00000040
#define IN_MOVED_TO 0x00000080
#define IN_CREATE 0x00000100
#define IN_DELETE 0x00000200
#define IN_DELETE_SELF 0x00000400
#define IN_MOVE_SELF 0x00000800
#define IN_UNMOUNT 0x00002000
#define IN_Q_OVERFLOW 0x00004000
#define IN_IGNORED 0x00008000
#define IN_ONLYDIR 0x01000000
#define IN_DONT_FOLLOW 0x02000000
#define IN_EXCL_UNLINK 0x04000000
#define IN_MASK_ADD 0x20000000
#define IN_ISDIR 0x40000000
#define IN_ONESHOT 0x80000000

int inotify_init(void);
int inotify_add_watch(int fd, const char *path, unsigned int mask);
int inotify_rm_watch(int fd, int wd);

#endif
EOF

cat > "$port/sys/ioctl.h" <<'EOF'
#ifndef ZHAOXU_SYS_IOCTL_H
#define ZHAOXU_SYS_IOCTL_H

#include <sys/types.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int fd, unsigned long request, ...);

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCGWINSZ 0x5413
#define FIONREAD 0x541B
#define TIOCSTI 0x5412
#define TIOCNOTTY 0x5409
#define TIOCSCTTY 0x540e
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFADDR 0x8915
#define SIOCGIFDSTADDR 0x8917
#define SIOCSIFDSTADDR 0x8918
#define SIOCGIFBRDADDR 0x8919
#define SIOCSIFBRDADDR 0x891a
#define SIOCSIFNAME 0x8923
#define SIOCGIFMAP 0x8970
#define SIOCGIFMETRIC 0x891d
#define SIOCSIFMETRIC 0x891e
#define SIOCGIFTXQLEN 0x8942
#define SIOCSIFTXQLEN 0x8943
#define SIOCSIFMAP 0x8971
#define SIOCGIFINDEX 0x8933
#define SIOCDIFADDR 0x8936
#define SIOCDEVPRIVATE 0x89f0
#define SIOCGIFCONF 0x8912
#define SIOCSIFADDR 0x8916
#define SIOCGIFNETMASK 0x891b
#define SIOCSIFNETMASK 0x891c
#define SIOCGIFMTU 0x8921
#define SIOCSIFMTU 0x8922
#define SIOCSIFHWADDR 0x8924
#define SIOCGIFHWADDR 0x8927

#define BLKROSET 0x125d
#define BLKFLSBUF 0x1261
#define BLKRRPART 0x125f

#endif
EOF

cat > "$port/sys/un.h" <<'EOF'
/* struct sockaddr_un lives in the socket header for this port. */
#ifndef ZHAOXU_SYS_UN_H
#define ZHAOXU_SYS_UN_H
#include <sys/socket.h>
#endif
EOF

cat > "$port/sys/socket.h" <<'EOF'
/* Socket surface matching the kernel's AF_UNIX ABI.  INET domains are
 * declared here but rejected by socket() until the BSD stack lands. */
#ifndef ZHAOXU_SYS_SOCKET_H
#define ZHAOXU_SYS_SOCKET_H

#include <sys/types.h>

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char padding[126];
};

#define MSG_PEEK 0x02
#define MSG_DONTWAIT 0x40

int mlock(const void *address, size_t length);
int munlock(const void *address, size_t length);
int mlockall(int flags);
int munlockall(void);

#define MCL_CURRENT 1
#define MCL_FUTURE 2

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#define CMSG_ALIGN(length) \
    (((length) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(length) (CMSG_ALIGN(length) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_LEN(length) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (length))
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_FIRSTHDR(msg) \
    ((msg)->msg_controllen >= sizeof(struct cmsghdr) \
         ? (struct cmsghdr *)(msg)->msg_control : (struct cmsghdr *)0)
#define CMSG_NXTHDR(msg, cmsg) \
    (((cmsg)->cmsg_len < sizeof(struct cmsghdr) || \
      (unsigned char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) >= \
          (unsigned char *)(msg)->msg_control + (msg)->msg_controllen) \
         ? (struct cmsghdr *)0 \
         : (struct cmsghdr *)((unsigned char *)(cmsg) + \
                              CMSG_ALIGN((cmsg)->cmsg_len)))

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    socklen_t msg_controllen;
    int msg_flags;
};

#define AF_UNSPEC 0
#define AF_UNIX 1
#define PF_UNIX AF_UNIX
#define AF_LOCAL AF_UNIX

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 0x800
#define SOCK_CLOEXEC 0x80000

#define SOL_SOCKET 1
#define SO_REUSEADDR 4
#define SO_SNDTIMEO 21
#define SO_RCVTIMEO 20
#define SO_MARK 36

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int fds[2]);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
int connect(int fd, const struct sockaddr *address, socklen_t length);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length);
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length);
ssize_t sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t recvmsg(int fd, struct msghdr *message, int flags);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int name, const void *value,
               socklen_t length);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getpeername(int fd, struct sockaddr *address, socklen_t *length);

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#endif
EOF

mkdir -p "$port/net"
cat > "$port/net/if_arp.h" <<'EOF'
#ifndef ZHAOXU_NET_IF_ARP_H
#define ZHAOXU_NET_IF_ARP_H

#include <sys/socket.h>

struct arpreq {
    struct sockaddr arp_pa;
    struct sockaddr arp_ha;
    int arp_flags;
    struct sockaddr arp_netmask;
    char arp_dev[16];
};

#define ARPHRD_ETHER 1
#define ARPHRD_LOOPBACK 772
#define ARPHRD_PPP 512
#define ARPHRD_SIT 776
#define ARPHRD_INFINIBAND 32

#define ATF_COM 0x02
#define ATF_PERM 0x04
#define ATF_PUBL 0x08

#endif
EOF

mkdir -p "$port/linux"
cat > "$port/linux/sysinfo.h" <<'EOF'
/* dmesg reaches for the kernel system-info structure under the Linux path. */
#ifndef ZHAOXU_LINUX_SYSINFO_H
#define ZHAOXU_LINUX_SYSINFO_H
#include <sys/sysinfo.h>
#endif
EOF

cat > "$port/sys/event.h" <<'EOF'
/* kqueue surface (FreeBSD subsystem); declarations until the BSD event
 * layer is reachable from Ring3. */
#ifndef ZHAOXU_SYS_EVENT_H
#define ZHAOXU_SYS_EVENT_H

#include <sys/types.h>

struct kevent {
    uintptr_t ident;
    short filter;
    unsigned short flags;
    unsigned int fflags;
    intptr_t data;
    void *udata;
};

#define EVFILT_READ (-1)
#define EVFILT_WRITE (-2)
#define EVFILT_AIO (-3)
#define EVFILT_VNODE (-4)
#define EVFILT_PROC (-5)
#define EVFILT_SIGNAL (-6)
#define EVFILT_TIMER (-7)

#define EV_ADD 0x0001
#define EV_DELETE 0x0002
#define EV_ENABLE 0x0004
#define EV_DISABLE 0x0008
#define EV_ONESHOT 0x0010
#define EV_CLEAR 0x0020
#define EV_RECEIPT 0x0040

#define EV_EOF 0x8000
#define EV_ERROR 0x4000

#define NOTE_DELETE 0x0001
#define NOTE_WRITE 0x0002
#define NOTE_EXTEND 0x0004
#define NOTE_ATTRIB 0x0008
#define NOTE_LINK 0x0010
#define NOTE_RENAME 0x0020
#define NOTE_REVOKE 0x0040

#define EV_SET(kevp, a, b, c, d, e, f) do { \
    (kevp)->ident = (a); \
    (kevp)->filter = (b); \
    (kevp)->flags = (c); \
    (kevp)->fflags = (d); \
    (kevp)->data = (e); \
    (kevp)->udata = (f); \
} while (0)

int kqueue(void);
int kevent(int fd, const struct kevent *changes, int change_count,
           struct kevent *events, int event_count,
           const struct timespec *timeout);

#endif
EOF

cat > "$port/resolv.h" <<'EOF'
/* Resolver entry points; the FreeBSD resolver supplies them in the BSD
 * subsystem, declarations only for the build. */
#ifndef ZHAOXU_RESOLV_H
#define ZHAOXU_RESOLV_H

#define NS_MAXDNAME 1025
#define NS_MAXCDNAME 255
#define NS_MAXLABEL 63
#define NS_MAXMSG 65535
#define NS_PACKETSZ 512

struct __res_state {
    int retrans;
    int retry;
    unsigned long options;
};

int res_init(void);
int res_query(const char *name, int class_, int type, unsigned char *answer,
              int answer_length);
int res_search(const char *name, int class_, int type, unsigned char *answer,
               int answer_length);
int dn_expand(const unsigned char *message, const unsigned char *end,
              const unsigned char *compressed, char *expanded,
              int expanded_length);
int res_mkquery(int operation, const char *name, int class_, int type,
                const void *data, int data_length, const void *newrr,
                unsigned char *buffer, int buffer_length);

#endif
EOF

cat > "$port/sys/sysinfo.h" <<'EOF'
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
EOF

cat > "$port/sys/klog.h" <<'EOF'
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
EOF

cat > "$port/net/if.h" <<'EOF'
#ifndef ZHAOXU_NET_IF_H
#define ZHAOXU_NET_IF_H

#include <sys/socket.h>

#define IF_NAMESIZE 16
#define IFNAMSIZ IF_NAMESIZE

#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_DEBUG 0x4
#define IFF_LOOPBACK 0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING 0x40
#define IFF_MULTICAST 0x1000
#define IFF_NOARP 0x80
#define IFF_PROMISC 0x100
#define IFF_ALLMULTI 0x200

struct ifconf {
    int ifc_len;
    union {
        char *ifcu_buf;
        struct ifreq *ifcu_req;
    } ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

struct ifmap {
    unsigned long mem_start;
    unsigned long mem_end;
    unsigned short base_addr;
    unsigned char irq;
    unsigned char dma;
    unsigned char port;
};

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct ifmap ifr_map;
        int ifr_qlen;
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short ifr_flags;
        int ifr_ifindex;
        int ifr_metric;
        int ifr_mtu;
        char ifr_slave[IFNAMSIZ];
        char ifr_newname[IFNAMSIZ];
        void *ifr_data;
    } ifr_ifru;
};

#define ifr_addr ifr_ifru.ifr_addr
#define ifr_dstaddr ifr_ifru.ifr_dstaddr
#define ifr_broadaddr ifr_ifru.ifr_broadaddr
#define ifr_netmask ifr_ifru.ifr_netmask
#define ifr_hwaddr ifr_ifru.ifr_hwaddr
#define ifr_flags ifr_ifru.ifr_flags
#define ifr_ifindex ifr_ifru.ifr_ifindex
#define ifr_metric ifr_ifru.ifr_metric
#define ifr_mtu ifr_ifru.ifr_mtu
#define ifr_slave ifr_ifru.ifr_slave
#define ifr_newname ifr_ifru.ifr_newname
#define ifr_data ifr_ifru.ifr_data
#define ifr_map ifr_ifru.ifr_map
#define ifr_qlen ifr_ifru.ifr_qlen

struct if_nameindex {
    unsigned int if_index;
    char *if_name;
};

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *table);

#endif
EOF

cat > "$port/netdb.h" <<'EOF'
/* Name-resolution surface: numeric literals resolve for real; DNS waits
 * for the FreeBSD network stack to be reachable from Ring 3. */
#ifndef ZHAOXU_NETDB_H
#define ZHAOXU_NETDB_H

#include <netinet/in.h>

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    unsigned int ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE 0x01
#define AI_CANONNAME 0x02
#define AI_NUMERICHOST 0x04
#define AI_ADDRCONFIG 0x20
#define AI_V4MAPPED 0x40
#define AI_ALL 0x80
#define NI_NUMERICHOST 0x01
#define NI_NUMERICSERV 0x02

#define EAI_NONAME -2
#define EAI_AGAIN -3
#define EAI_FAIL -4
#define EAI_SOCKTYPE -5
#define EAI_FAMILY -6
#define EAI_SERVICE -8
#define EAI_MEMORY -10
#define EAI_SYSTEM -11

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result);
void freeaddrinfo(struct addrinfo *result);
const char *gai_strerror(int error);
int getnameinfo(const void *address, unsigned int address_length, char *host,
                unsigned int host_length, char *service,
                unsigned int service_length, int flags);
struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};
struct hostent *gethostbyname(const char *name);

struct servent {
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
};
struct servent *getservbyport(int port, const char *protocol);
struct servent *getservbyname(const char *name, const char *protocol);
extern int h_errno;

#define HOST_NOT_FOUND 1
#define TRY_AGAIN 2
#define NO_RECOVERY 3
#define NO_DATA 4
const char *hstrerror(int error);

#endif
EOF

mkdir -p "$port/netinet" "$port/arpa"
cat > "$port/netinet/in.h" <<'EOF'
#ifndef ZHAOXU_NETINET_IN_H
#define ZHAOXU_NETINET_IN_H
#include <sys/types.h>
typedef unsigned short in_port_t;
typedef unsigned int in_addr_t;
struct in_addr {
    in_addr_t s_addr;
};
struct in6_addr {
    unsigned char s6_addr[16];
};
struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
struct sockaddr_in6 {
    unsigned short sin6_family;
    unsigned short sin6_port;
    unsigned int sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned int sin6_scope_id;
};
#define AF_INET 2
#define AF_INET6 10
#define INADDR_ANY 0
#define INADDR_NONE 0xFFFFFFFFu
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_ICMPV6 58
#define SOL_IP 0
#define IPPROTO_IPV6 41
#define IP_TTL 2
#define IP_RECVTTL 12
#define IPV6_UNICAST_HOPS 16
#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_LOOP 19
#define IPV6_ADD_MEMBERSHIP 20
#define IPV6_RECVHOPLIMIT 51
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IP_DROP_MEMBERSHIP 36

struct ip_mreq {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
};
unsigned short htons(unsigned short value);
unsigned short ntohs(unsigned short value);
unsigned int htonl(unsigned int value);
unsigned int ntohl(unsigned int value);
#endif
EOF

cat > "$port/arpa/inet.h" <<'EOF'
#ifndef ZHAOXU_ARPA_INET_H
#define ZHAOXU_ARPA_INET_H
#include <netinet/in.h>
const char *inet_ntop(int family, const void *address, char *text,
                      unsigned int size);
int inet_pton(int family, const char *text, void *address);
in_addr_t inet_addr(const char *text);
char *inet_ntoa(struct in_addr address);
#endif
EOF

cat > "$port/sys/uio.h" <<'EOF'
#ifndef ZHAOXU_SYS_UIO_H
#define ZHAOXU_SYS_UIO_H
#include <sys/types.h>
struct iovec {
    void *iov_base;
    size_t iov_len;
};
ssize_t readv(int fd, const struct iovec *iov, int count);
ssize_t writev(int fd, const struct iovec *iov, int count);
#endif
EOF

cat > "$port/syslog.h" <<'EOF'
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
EOF

cat > "$port/stdio.h" <<'EOF'
#ifndef ZHAOXU_STDIO_OVERLAY_H
#define ZHAOXU_STDIO_OVERLAY_H

#include_next <stdio.h>

/* POSIX line readers newlib does not declare in this build. */
ssize_t getline(char **line, size_t *capacity, FILE *stream);
ssize_t getdelim(char **line, size_t *capacity, int delimiter, FILE *stream);

#endif
EOF

cat > "$port/time.h" <<'EOF'
#ifndef ZHAOXU_TIME_OVERLAY_H
#define ZHAOXU_TIME_OVERLAY_H

#include_next <time.h>

/* Sleep for a relative interval; the implementation sleeps through the
 * kernel timer (sys_sleep) once the libgloss wrapper lands. */
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

int nanosleep(const struct timespec *request, struct timespec *remaining);
int clock_gettime(int clock_id, struct timespec *value);
int clock_settime(int clock_id, const struct timespec *value);

extern long timezone;
extern int daylight;
char *strptime(const char *text, const char *format, struct tm *result);

#endif
EOF

cat > "$port/sys/xattr.h" <<'EOF'
/* Extended attributes have no kernel support yet; declarations only. */
#ifndef ZHAOXU_SYS_XATTR_H
#define ZHAOXU_SYS_XATTR_H

#include <sys/types.h>

ssize_t getxattr(const char *path, const char *name, void *value,
                 size_t size);
ssize_t lgetxattr(const char *path, const char *name, void *value,
                  size_t size);
int setxattr(const char *path, const char *name, const void *value,
             size_t size, int flags);
int lsetxattr(const char *path, const char *name, const void *value,
              size_t size, int flags);
int removexattr(const char *path, const char *name);
int lremovexattr(const char *path, const char *name);
ssize_t listxattr(const char *path, char *list, size_t size);
ssize_t llistxattr(const char *path, char *list, size_t size);
ssize_t flistxattr(int fd, char *list, size_t size);
ssize_t fgetxattr(int fd, const char *name, void *value, size_t size);
int fsetxattr(int fd, const char *name, const void *value, size_t size,
              int flags);
int fremovexattr(int fd, const char *name);

#endif
EOF

cat > "$port/wchar.h" <<'EOF'
#ifndef ZHAOXU_WCHAR_OVERLAY_H
#define ZHAOXU_WCHAR_OVERLAY_H

#include_next <wchar.h>

/* Column width of one wide character; East Asian wide ranges are not in
 * the table yet, so non-ASCII characters count as one column. */
int wcwidth(int character);

#endif
EOF

cat > /tmp/wcwidth.c <<'EOF'
/* Minimal wcwidth: ASCII control -1, NUL 0, everything else one column. */
int wcwidth(int character)
{
    if (character == 0)
        return 0;
    if (character < 32 || (character >= 0x7f && character < 0xa0))
        return -1;
    return 1;
}
EOF
clang --target=x86_64-elf \
    --sysroot=/home/tindo/newlib/install/x86_64-elf \
    -isystem /home/tindo/newlib/install/x86_64-elf/include \
    -fPIC -c /tmp/wcwidth.c -o /tmp/wcwidth.o
ar rcs /home/tindo/toybox/libport.a /tmp/wcwidth.o
ar rcs /home/tindo/toybox/libzospassword.a

# Rebuild the FreeBSD regex objects as PIC so they can live in a shared ZXE.
mkdir -p /home/tindo/toybox/regex-build
cd /home/tindo/toybox/regex-build
for src in regcomp regerror regexec regfree; do
    clang --target=x86_64-elf \
        --sysroot=/home/tindo/newlib/install/x86_64-elf \
        -isystem /home/tindo/newlib/install/x86_64-elf/include \
        -I/home/tindo/toybox/port-include \
        -I"$SRC/src/third_party/bsd-regex" \
        -fPIC -O2 -D__ZHAOXU_OS__ \
        -c "$SRC/src/third_party/bsd-regex/$src.c" \
        -o "$src.o" || exit 1
done
ar rcs /home/tindo/toybox/libregex.a ./*.o

cd /home/tindo/toybox/src
# Rule 39: toybox is not trimmed.  Restore its full default configuration.
make defconfig >/dev/null 2>&1 || true

# A single executable wrapper pins the cross target for both compile and
# link; inline CC values with spaces do not survive toybox's probing.
cat > /home/tindo/toybox/cc-elf <<'EOF'
#!/bin/sh
exec clang --target=x86_64-elf \
    --sysroot=/home/tindo/newlib/install/x86_64-elf "$@"
EOF
chmod +x /home/tindo/toybox/cc-elf

# FreeBSD 15.1 libcrypt + libmd + SHA-2 (rule 39) for mkpasswd and login.
CRYPT_SRC="$SRC/src/third_party/freebsd-libcrypt"
CRYPT_PORT="$SRC/src/third_party/freebsd-libcrypt-port"
mkdir -p /home/tindo/toybox/fbcrypt
cd /home/tindo/toybox/fbcrypt
for source_file in "$CRYPT_SRC"/libcrypt/crypt.c "$CRYPT_SRC"/libcrypt/crypt-md5.c \
                   "$CRYPT_SRC"/libcrypt/crypt-nthash.c "$CRYPT_SRC"/libcrypt/crypt-sha256.c \
                   "$CRYPT_SRC"/libcrypt/crypt-sha512.c "$CRYPT_SRC"/libcrypt/misc.c \
                   "$CRYPT_SRC"/libmd/md4c.c "$CRYPT_SRC"/libmd/md5c.c \
                   "$CRYPT_SRC"/sha2/sha256c.c "$CRYPT_SRC"/sha2/sha512c.c \
                   "$CRYPT_PORT"/src/err.c; do
    object=$(basename "$source_file" .c).o
    clang --target=x86_64-elf \
        --sysroot=/home/tindo/newlib/install/x86_64-elf \
        -isystem /home/tindo/newlib/install/x86_64-elf/include \
        -I/home/tindo/toybox/port-include \
        -I"$CRYPT_PORT/include" -I"$CRYPT_SRC/libmd" -I"$CRYPT_SRC/sha2" \
        -I"$CRYPT_SRC/libcrypt" \
        -fPIC -O2 -w -c "$source_file" -o "$object" || exit 1
done
ar rcs /home/tindo/toybox/libfbcrypt.a ./*.o
cd /home/tindo/toybox/src

# Runtime pieces every ZXE needs: crt0 and the POSIX syscall glue.
clang --target=x86_64-elf \
    --sysroot=/home/tindo/newlib/install/x86_64-elf \
    -isystem /home/tindo/newlib/install/x86_64-elf/include \
    -c "$SRC/src/user/lib/posix/crt0.S" \
    -o /home/tindo/toybox/crt0.o
clang --target=x86_64-elf \
    --sysroot=/home/tindo/newlib/install/x86_64-elf \
    -isystem /home/tindo/newlib/install/x86_64-elf/include \
    -I/home/tindo/toybox/port-include \
    -I"$SRC/src/user" \
    -I"$SRC/src/kernel/include" \
    -fPIC -c "$SRC/src/user/lib/posix/libgloss.c" \
    -o /home/tindo/toybox/libgloss.o

# The rest of the POSIX surface (accounts, dirent, paths, limits, select,
# signals, termios, time) lives in its own files; link them all into one
# archive so toybox resolves the same surface a normal ZXE program gets.
mkdir -p /home/tindo/toybox/zosposix
cd /home/tindo/toybox/zosposix
for f in $(cd "$SRC/src/user/lib/posix" && ls posix_*.c | sed "s/\.c$//"); do
    clang --target=x86_64-elf \
        --sysroot=/home/tindo/newlib/install/x86_64-elf \
        -isystem /home/tindo/newlib/install/x86_64-elf/include \
        -I/home/tindo/toybox/port-include \
        -I"$SRC/src/user/lib/posix" \
        -I"$SRC/src/user/lib/posix/include" \
        -I"$SRC/src/user" \
        -D__ZHAOXU_OS__ -fPIC -O2 -w \
        -c "$SRC/src/user/lib/posix/$f.c" \
        -o "$f.o" || exit 1
done
ar rcs /home/tindo/toybox/libzosposix.a ./*.o
cd /home/tindo/toybox/src

# The sysconf guards must run after every header has been rewritten.
awk 'prev !~ /^#ifndef / && /^#define _(SC|CS|PC)_/ { name=$2; print "#ifndef " name; print; print "#endif"; prev=$0; next } { print; prev=$0 }' \
    "$port/unistd.h" > "$port/unistd.h.tmp" && mv "$port/unistd.h.tmp" "$port/unistd.h"

make -j8 toybox V=1 CC=/home/tindo/toybox/cc-elf HOSTCC=cc \
    CFLAGS="-isystem /home/tindo/newlib/install/x86_64-elf/include -I/home/tindo/toybox/port-include -D__ZHAOXU_OS__ -fpic -fno-stack-protector -O2" \
    LDFLAGS="-fuse-ld=lld -shared -Wl,-T,/home/tindo/toybox/user.ld -Wl,-Bsymbolic,--build-id=none -Wl,--version-script,/home/tindo/toybox/fbsd.ver -nostdlib /home/tindo/toybox/crt0.o /home/tindo/toybox/libgloss.o /home/tindo/toybox/libfbcrypt.a -L/home/tindo/toybox -lzosposix -lregex -lport -L/home/tindo/newlib/install/x86_64-elf/lib -lc -lm" > /tmp/tb9.log 2>&1 || true
grep -E "error:|fatal error|undefined reference" /tmp/tb9.log | head -12
tail -3 /tmp/tb9.log
ls -l toybox 2>/dev/null || echo "no toybox binary"
