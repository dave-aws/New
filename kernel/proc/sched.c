/* NOVA OS — preemptive round-robin scheduler and process table.
 *
 * Context switching is done by swapping trap frames: every thread owns a
 * kernel stack whose top holds the frame the interrupt stub will restore.
 * schedule_from_trap() returns the frame of the thread that should run next,
 * and the stub resumes from there.  This keeps one switch path for timer
 * interrupts, blocking syscalls and brand-new threads.
 */
#include <nova/types.h>
#include <nova/sched.h>
#include <nova/mm.h>
#include <nova/trap.h>
#include <nova/io.h>
#include <nova/log.h>
#include <nova/string.h>

static struct thread thread_pool[MAX_THREADS];
static u8 kstack_pool[MAX_THREADS][KSTACK_SIZE] aligned(16);
static struct process proc_pool[MAX_PROCESSES];

static struct thread *current;
static struct thread *idle_thread;
static struct process *current_proc;
static u64 active_pml4;
static volatile bool resched_flag;
static int next_tid = 1;
static int next_pid = 1;

static u64 ticks_now;
static u64 rr_cursor;

static struct thread *pick_ready(void)
{
    int best_prio = -1;
    struct thread *pick = NULL;

    for (int i = 0; i < MAX_THREADS; i++) {
        int idx = (int)((rr_cursor + (u64)i) % (u64)MAX_THREADS);
        struct thread *t = &thread_pool[idx];
        if (t->state != THREAD_READY && t->state != THREAD_RUNNING)
            continue;
        if (t->priority > best_prio) {
            best_prio = t->priority;
            pick = t;
        }
    }
    if (pick)
        rr_cursor = (u64)(pick - thread_pool) + 1;
    return pick;
}

static void idle_entry(void *arg)
{
    (void)arg;
    for (;;) {
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

static void thread_bootstrap(void *arg)
{
    struct thread *t = arg;
    if (t->kentry)
        t->kentry(t->karg);
    thread_exit();
}

static struct thread *thread_alloc(const char *name)
{
    for (int i = 0; i < MAX_THREADS; i++) {
        struct thread *t = &thread_pool[i];
        if (t->state != THREAD_UNUSED)
            continue;
        memset(t, 0, sizeof(*t));
        t->tid = next_tid++;
        t->kstack_base = (u64)kstack_pool[i];
        t->kstack_top = (u64)kstack_pool[i] + KSTACK_SIZE;
        t->ticks_left = TIME_SLICE;
        t->state = THREAD_READY;
        strncpy(t->name, name ? name : "thread", sizeof(t->name) - 1);
        return t;
    }
    return NULL;
}

static struct regs *build_kernel_frame(struct thread *t, u64 entry, u64 arg)
{
    u64 frame_addr = t->kstack_top - sizeof(struct regs);
    struct regs *f = (struct regs *)frame_addr;
    memset(f, 0, sizeof(*f));
    f->rip = entry;
    f->cs = SEG_KCODE;
    f->rflags = 0x202;
    f->ss = SEG_KDATA;
    f->rsp = t->kstack_top;
    f->rdi = arg;
    t->rsp = frame_addr;
    return f;
}

struct thread *thread_create_kernel(const char *name, void (*entry)(void *),
                                    void *arg, int priority)
{
    u64 flags = irq_save();
    struct thread *t = thread_alloc(name);
    if (!t) {
        irq_restore(flags);
        return NULL;
    }
    t->kentry = entry;
    t->karg = arg;
    t->priority = priority;
    t->proc = NULL;
    build_kernel_frame(t, (u64)thread_bootstrap, (u64)t);
    irq_restore(flags);
    return t;
}

struct thread *thread_create_user(struct process *p, u64 entry, u64 user_stack,
                                  u64 arg, const char *name)
{
    u64 flags = irq_save();
    struct thread *t = thread_alloc(name);
    if (!t) {
        irq_restore(flags);
        return NULL;
    }
    t->priority = 1;
    t->proc = p;
    u64 frame_addr = t->kstack_top - sizeof(struct regs);
    struct regs *f = (struct regs *)frame_addr;
    memset(f, 0, sizeof(*f));
    f->rip = entry;
    f->cs = SEG_UCODE_RPL3;
    f->rflags = 0x202;
    f->rsp = user_stack;
    f->ss = SEG_UDATA_RPL3;
    f->rdi = arg;
    f->rcx = arg;                 /* Win64 first argument */
    t->rsp = frame_addr;

    t->next_ready = p->threads;
    p->threads = t;
    if (!p->main_thread)
        p->main_thread = t;
    irq_restore(flags);
    return t;
}

void sched_init(void)
{
    memset(thread_pool, 0, sizeof(thread_pool));
    memset(proc_pool, 0, sizeof(proc_pool));
    current = NULL;
    current_proc = NULL;
    active_pml4 = vmm_kernel_pml4();
    rr_cursor = 0;

    /* The boot context (kmain) becomes a real schedulable thread as soon as
     * it takes its first trap; until then its saved rsp is unused. */
    struct thread *boot = thread_alloc("boot");
    boot->priority = 1;
    boot->state = THREAD_RUNNING;
    current = boot;

    idle_thread = thread_alloc("idle");
    idle_thread->priority = 0;
    idle_thread->kentry = NULL;
    idle_thread->state = THREAD_READY;
    build_kernel_frame(idle_thread, (u64)idle_entry, 0);
    tss_set_rsp0(boot->kstack_top);

    kprintf("[sched] %d thread slots, %d KiB stacks, idle tid=%d\n",
            MAX_THREADS, KSTACK_SIZE / 1024, idle_thread->tid);
}

struct thread *current_thread(void) { return current; }

int sched_thread_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_THREADS; i++)
        if (thread_pool[i].state != THREAD_UNUSED &&
            thread_pool[i].state != THREAD_DEAD)
            n++;
    return n;
}

struct regs *schedule_from_trap(struct regs *r)
{
    if (current) {
        current->rsp = (u64)r;
        if (current->state == THREAD_RUNNING)
            current->state = THREAD_READY;
    }
    resched_flag = false;

    struct thread *next = pick_ready();
    if (!next)
        next = idle_thread;
    if (!next)
        return r;

    if (next == current && next != idle_thread) {
        current->state = THREAD_RUNNING;
        current->ticks_left = TIME_SLICE;
        return r;
    }

    current = next;
    next->state = THREAD_RUNNING;
    next->ticks_left = TIME_SLICE;

    if (next->proc) {
        current_proc = next->proc;
        if (next->proc->pml4 && next->proc->pml4 != active_pml4) {
            active_pml4 = next->proc->pml4;
            vmm_switch(active_pml4);
        }
    } else {
        current_proc = NULL;
        if (active_pml4 != vmm_kernel_pml4()) {
            active_pml4 = vmm_kernel_pml4();
            vmm_switch(active_pml4);
        }
    }
    tss_set_rsp0(next->kstack_top);
    return (struct regs *)next->rsp;
}

bool need_resched(void) { return resched_flag; }
bool syscall_need_resched(void) { return resched_flag; }
void set_need_resched(void) { resched_flag = true; }

void sched_tick(void)
{
    ticks_now++;
    if (current && current != idle_thread) {
        current->user_ticks++;
        if (current->ticks_left > 0)
            current->ticks_left--;
        if (current->ticks_left == 0)
            set_need_resched();
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        struct thread *t = &thread_pool[i];
        if (t->state == THREAD_SLEEPING && t->wake_tick <= ticks_now) {
            t->state = THREAD_READY;
            t->ticks_left = TIME_SLICE;
        }
    }
}

void thread_yield(void)
{
    set_need_resched();
    __asm__ volatile("int $0x80" : : "a"(1) : "memory");
}

void thread_sleep_ms(u64 ms)
{
    if (!current || current == idle_thread)
        return;
    extern u32 pit_hz(void);
    u64 hz = pit_hz();
    u64 ticks = MAX(1, (ms * hz) / 1000);
    current->wake_tick = ticks_now + ticks;
    current->state = THREAD_SLEEPING;
    set_need_resched();
    __asm__ volatile("int $0x80" : : "a"(1) : "memory");
}

void thread_block(void)
{
    if (!current || current == idle_thread)
        return;
    current->state = THREAD_BLOCKED;
    set_need_resched();
    __asm__ volatile("int $0x80" : : "a"(1) : "memory");
}

void thread_unblock(struct thread *t)
{
    if (!t || t->state == THREAD_DEAD || t->state == THREAD_UNUSED)
        return;
    t->state = THREAD_READY;
    t->ticks_left = TIME_SLICE;
}

void thread_exit(void)
{
    u64 flags = irq_save();
    (void)flags;
    if (current) {
        current->state = THREAD_DEAD;
        struct process *p = current->proc;
        if (p) {
            struct thread **pp = &p->threads;
            while (*pp) {
                if (*pp == current) {
                    *pp = current->next_ready;
                    break;
                }
                pp = &(*pp)->next_ready;
            }
            if (p->main_thread == current)
                p->main_thread = p->threads;
        }
    }
    set_need_resched();
    __asm__ volatile("int $0x80" : : "a"(1) : "memory");
    for (;;) {
        cpu_cli();
        cpu_halt();
    }
}

/* ------------------------------------------------------------------ proc -- */

struct process *process_create(const char *name)
{
    u64 flags = irq_save();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &proc_pool[i];
        if (p->state != PROC_UNUSED)
            continue;
        memset(p, 0, sizeof(*p));
        p->pid = next_pid++;
        p->state = PROC_RUNNING;
        p->pml4 = vmm_kernel_pml4();
        p->start_tick = ticks_now;
        strncpy(p->name, name ? name : "process", sizeof(p->name) - 1);
        strcpy(p->cwd, "C:\\");
        irq_restore(flags);
        return p;
    }
    irq_restore(flags);
    return NULL;
}

struct process *process_create_user(const char *name, u64 image_base, u64 image_size)
{
    struct process *p = process_create(name);
    if (!p)
        return NULL;
    u64 pml4 = vmm_new_address_space();
    if (!pml4) {
        p->state = PROC_UNUSED;
        return NULL;
    }
    p->pml4 = pml4;
    p->image_base = image_base;
    p->image_size = image_size;
    return p;
}

struct process *process_current(void) { return current_proc; }

struct process *process_by_pid(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (proc_pool[i].state != PROC_UNUSED && proc_pool[i].pid == pid)
            return &proc_pool[i];
    return NULL;
}

void process_set_current(struct process *p) { current_proc = p; }

int process_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (proc_pool[i].state != PROC_UNUSED)
            n++;
    return n;
}

struct process *process_iter(int index)
{
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_pool[i].state == PROC_UNUSED)
            continue;
        if (n++ == index)
            return &proc_pool[i];
    }
    return NULL;
}

void process_set_cmdline(struct process *p, const char *cmdline)
{
    if (!p || !cmdline)
        return;
    strncpy(p->cmdline, cmdline, sizeof(p->cmdline) - 1);
}

void process_sleep_ticks(u64 ticks)
{
    if (!current)
        return;
    current->wake_tick = ticks_now + ticks;
    current->state = THREAD_SLEEPING;
    set_need_resched();
    __asm__ volatile("int $0x80" : : "a"(1) : "memory");
}

void process_terminate(struct process *p, int code)
{
    if (!p || p->state == PROC_UNUSED)
        return;
    p->exit_code = code;
    p->state = PROC_ZOMBIE;
    for (int i = 0; i < MAX_THREADS; i++) {
        struct thread *t = &thread_pool[i];
        if (t->proc == p && t->state != THREAD_UNUSED) {
            t->state = THREAD_DEAD;
            if (t == current)
                set_need_resched();
        }
    }
}

void process_exit(int code)
{
    struct process *p = current_proc;
    if (p) {
        p->exit_code = code;
        p->state = PROC_ZOMBIE;
    }
    if (current)
        current->state = THREAD_DEAD;
    set_need_resched();
    for (;;) {
        __asm__ volatile("int $0x80" : : "a"(1) : "memory");
        cpu_halt();
    }
}

int threads_snapshot(struct thread **out, int max, int *states)
{
    int n = 0;
    for (int i = 0; i < MAX_THREADS && n < max; i++) {
        if (thread_pool[i].state == THREAD_UNUSED)
            continue;
        out[n] = &thread_pool[i];
        if (states)
            states[n] = thread_pool[i].state;
        n++;
    }
    return n;
}
