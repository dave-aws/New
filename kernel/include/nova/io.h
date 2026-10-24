/* NOVA OS — architecture primitives: port I/O, MSRs, control registers. */
#ifndef NOVA_IO_H
#define NOVA_IO_H

#include <nova/types.h>

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(u16 port, u16 val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline u16 inw(u16 port)
{
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(u16 port, u32 val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline u32 inl(u16 port)
{
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void io_wait(void) { outb(0x80, 0); }

static inline void cpu_halt(void) { __asm__ volatile("hlt"); }
static inline void cpu_pause(void) { __asm__ volatile("pause"); }
static inline void cpu_relax(void) { __asm__ volatile("pause"); }
static inline void cpu_cli(void) { __asm__ volatile("cli"); }
static inline void cpu_sti(void) { __asm__ volatile("sti"); }
static inline void memory_barrier(void) { __asm__ volatile("" ::: "memory"); }
static inline void cpu_relax_full(void) { __asm__ volatile("mfence" ::: "memory"); }

static inline u64 irq_save(void)
{
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(u64 flags)
{
    if (flags & (1ULL << 9))
        __asm__ volatile("sti" ::: "memory");
    else
        __asm__ volatile("cli" ::: "memory");
}

static inline u64 read_cr0(void) { u64 v; __asm__ volatile("mov %%cr0,%0" : "=r"(v)); return v; }
static inline void write_cr0(u64 v) { __asm__ volatile("mov %0,%%cr0" : : "r"(v) : "memory"); }
static inline u64 read_cr2(void) { u64 v; __asm__ volatile("mov %%cr2,%0" : "=r"(v)); return v; }
static inline u64 read_cr3(void) { u64 v; __asm__ volatile("mov %%cr3,%0" : "=r"(v)); return v; }
static inline void write_cr3(u64 v) { __asm__ volatile("mov %0,%%cr3" : : "r"(v) : "memory"); }
static inline u64 read_cr4(void) { u64 v; __asm__ volatile("mov %%cr4,%0" : "=r"(v)); return v; }
static inline void write_cr4(u64 v) { __asm__ volatile("mov %0,%%cr4" : : "r"(v) : "memory"); }

static inline u64 rdmsr(u32 msr)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}
static inline void wrmsr(u32 msr, u64 val)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((u32)val), "d"((u32)(val >> 32)));
}

#define MSR_EFER         0xC0000080
#define MSR_STAR         0xC0000081
#define MSR_LSTAR        0xC0000082
#define MSR_FMASK        0xC0000084
#define MSR_FS_BASE      0xC0000100
#define MSR_GS_BASE      0xC0000101
#define MSR_KERNEL_GS    0xC0000102

#define EFER_SCE    (1ULL << 0)
#define EFER_LME    (1ULL << 8)
#define EFER_NXE    (1ULL << 11)

noreturn void arch_halt_forever(void);
void gdt_load(void *gdt_ptr, u16 code, u16 data);
void tss_load(u16 selector);
void jump_usermode(u64 entry, u64 stack, u64 arg) noreturn;

#endif /* NOVA_IO_H */
