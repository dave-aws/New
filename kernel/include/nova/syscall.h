/* NOVA OS — native system call numbers.
 *
 * ABI: rax = number, rdi/rsi/rdx/r10/r8/r9 = arguments, return in rax.
 * Issued with `int $0x80`.  Negative returns are errors.
 */
#ifndef NOVA_SYSCALL_H
#define NOVA_SYSCALL_H

#define SYS_EXIT          1
#define SYS_YIELD         2
#define SYS_WRITE         3
#define SYS_READ          4
#define SYS_OPEN          5
#define SYS_CLOSE         6
#define SYS_LSEEK         7
#define SYS_SLEEP         8
#define SYS_GETPID        9
#define SYS_TIME_MS      10
#define SYS_VMALLOC      11
#define SYS_VMFREE       12
#define SYS_FBINFO       13
#define SYS_WIN32        14   /* Win32 personality: r11 = API id */
#define SYS_WIN_CREATE   15
#define SYS_WIN_PRESENT  16
#define SYS_DEBUG        17
#define SYS_GETENV       18
#define SYS_SETENV       19
#define SYS_FS_READDIR   20
#define SYS_FS_STAT      21
#define SYS_FS_MKDIR     22
#define SYS_FS_UNLINK    23
#define SYS_WIN_POLL     24
#define SYS_WIN_DRAW     25
#define SYS_TICKS        26
#define SYS_THREAD_CREATE 27
#define SYS_WIN32_W      28   /* Win32 personality with wide strings */
#define SYS_MAX          29

#ifndef __ASSEMBLER__
struct regs;
u64 syscall_dispatch(struct regs *r);
#endif

#endif /* NOVA_SYSCALL_H */
