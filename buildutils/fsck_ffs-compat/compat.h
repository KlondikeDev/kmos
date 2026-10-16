/* Shims for building FreeBSD's fsck_ffs on glibc, for checking Kunix's UFS2 images. */
#pragma once
#define __FBSDID(s)
#define __unused __attribute__((unused))
#define __dead2 __attribute__((noreturn))
#define __printflike(a, b) __attribute__((format(printf, a, b)))
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __DECONST(t, v) ((t)(uintptr_t)(const void*)(v))
#define nitems(x) (sizeof(x) / sizeof((x)[0]))
#include <stdint.h>
#include <sys/types.h>
typedef uint64_t ufs2_daddr_t_dummy;
typedef int64_t quad_t;
typedef uint64_t u_quad_t;
#ifndef MAXBSIZE
#define MAXBSIZE 65536
#endif
#ifndef DEV_BSIZE
#define DEV_BSIZE 512
#endif
#ifndef DEV_BSHIFT
#define DEV_BSHIFT 9
#endif
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <sys/queue.h>
struct oexport_args { int ex_flags; };
#define LINK_MAX 32767
#define SF_SNAPSHOT 0
#define st_flags st_mode
#define setproctitle(...) do { } while (0)
#define btodb(bytes) ((uint64_t)(bytes) >> DEV_BSHIFT)
#define SIGINFO SIGUSR1
#define CLOCK_REALTIME_PRECISE CLOCK_REALTIME
#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = TAILQ_FIRST((head)); (var) && ((tvar) = TAILQ_NEXT((var), field), 1); (var) = (tvar))
#endif
#ifndef TAILQ_FOREACH_REVERSE_SAFE
#define TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar) \
    for ((var) = TAILQ_LAST((head), headname); (var) && ((tvar) = TAILQ_PREV((var), headname, field), 1); (var) = (tvar))
#endif
#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = LIST_FIRST((head)); (var) && ((tvar) = LIST_NEXT((var), field), 1); (var) = (tvar))
#endif
#ifndef TAILQ_FIRST
#define TAILQ_FIRST(head) ((head)->tqh_first)
#endif
#ifndef TAILQ_NEXT
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)
#endif
#ifndef TAILQ_LAST
#define TAILQ_LAST(head, headname) (*(((struct headname *)((head)->tqh_last))->tqh_last))
#endif
#ifndef TAILQ_PREV
#define TAILQ_PREV(elm, headname, field) (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))
#endif
#ifndef LIST_FIRST
#define LIST_FIRST(head) ((head)->lh_first)
#endif
#ifndef LIST_NEXT
#define LIST_NEXT(elm, field) ((elm)->field.le_next)
#endif
#define SF_IMMUTABLE 0
#define SF_NOUNLINK 0
#include <sys/sysctl.h>
#include <stdlib.h>
#define getprogname() program_invocation_short_name
static inline int sysctlnametomib(const char* n, int* mib, size_t* sz) { (void)n; (void)mib; (void)sz; errno = ENOENT; return -1; }
/* BSD's statfs carries mount information glibc's does not; fsck only uses
 * it to look for a mounted filesystem, and an image file never is one. */
#include <sys/vfs.h>
#define statfs bsd_statfs
#define fstatfs bsd_fstatfs
#define getmntinfo bsd_getmntinfo
struct bsd_statfs { uint64_t f_flags; char f_fstypename[16]; char f_mntonname[1024]; char f_mntfromname[1024]; };
static inline int bsd_statfs(const char* p, struct bsd_statfs* s) { (void)p; (void)s; errno = ENOSYS; return -1; }
static inline int bsd_fstatfs(int fd, struct bsd_statfs* s) { (void)fd; (void)s; errno = ENOSYS; return -1; }
static inline int bsd_getmntinfo(struct bsd_statfs** m, int f) { (void)m; (void)f; return 0; }
#define MNT_NOWAIT 2
#define MNT_WAIT 1
#define MNT_RDONLY 0x1
#define MNT_SOFTDEP 0x200000
#define MNT_SUJ 0x100000000ULL
#define MNT_UPDATE 0x10000
#define MNT_RELOAD 0x40000
#include <sys/mount.h>
#define MNT_ROOTFS 0x4000
#define MNAMELEN 1024
static inline int nmount(void* iov, unsigned n, int f) { (void)iov; (void)n; (void)f; errno = ENOSYS; return -1; }
