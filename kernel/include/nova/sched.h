/* NOVA OS — threads, processes and scheduling. */
#ifndef NOVA_SCHED_H
#define NOVA_SCHED_H

#include <nova/types.h>
#include <nova/trap.h>

#define MAX_THREADS   64
#define MAX_PROCESSES 32
#define KSTACK_SIZE   (32 * 1024)
#define TIME_SLICE    5

enum thread_state {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_DEAD,
};

enum process_state {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_ZOMBIE,
};

struct process;

struct thread {
    u64 rsp;                       /* saved trap frame */
    u64 kstack_base;
    u64 kstack_top;
    int tid;
    volatile int state;
    int priority;
    int ticks_left;
    u64 wake_tick;
    u64 user_ticks;
    struct process *proc;
    void (*kentry)(void *);
    void *karg;
    char name[32];
    struct thread *next_ready;
};

struct process {
    int pid;
    int state;
    int exit_code;
    u64 pml4;
    struct thread *main_thread;
    struct thread *threads;
    char name[64];
    char image_path[256];
    char cwd[256];
    char cmdline[512];
    u64 heap_base;
    u64 heap_top;
    u64 image_base;
    u64 image_size;
    u64 start_tick;
    void *win32;                   /* struct win32_process * */
    void *console;                 /* struct console * */
    void *fds[32];                 /* struct vfs_file * */
    struct process *next;
};

void sched_init(void);
struct thread *thread_create_kernel(const char *name, void (*entry)(void *),
                                    void *arg, int priority);
struct thread *thread_create_user(struct process *p, u64 entry, u64 user_stack,
                                  u64 arg, const char *name);
void thread_exit(void) noreturn;
void thread_block(void);
void thread_unblock(struct thread *t);
void thread_sleep_ms(u64 ms);
void thread_yield(void);
struct thread *current_thread(void);
int sched_thread_count(void);
void sched_tick(void);

struct regs *schedule_from_trap(struct regs *r);
bool need_resched(void);
bool syscall_need_resched(void);
void set_need_resched(void);

int  threads_snapshot(struct thread **out, int max, int *states);

/* ------------------------------------------------------------------ proc -- */
struct process *process_create(const char *name);
struct process *process_create_user(const char *name, u64 image_base, u64 image_size);
struct process *process_current(void);
struct process *process_by_pid(int pid);
void process_set_current(struct process *p);
void process_exit(int code) noreturn;
void process_terminate(struct process *p, int code);
int  process_count(void);
struct process *process_iter(int index);
void process_set_cmdline(struct process *p, const char *cmdline);
void process_sleep_ticks(u64 ticks);

#endif /* NOVA_SCHED_H */
