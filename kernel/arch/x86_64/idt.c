/* NOVA OS — IDT, exception handling and trap dispatch. */
#include <nova/types.h>
#include <nova/io.h>
#include <nova/trap.h>
#include <nova/log.h>
#include <nova/string.h>

struct idt_entry {
    u16 off_lo;
    u16 selector;
    u8  ist;
    u8  flags;
    u16 off_mid;
    u32 off_hi;
    u32 zero;
} packed;

struct idt_ptr {
    u16 limit;
    u64 base;
} packed;

static struct idt_entry idt[256] aligned(16);
static struct idt_ptr idtr;

static void (*irq_handlers[16])(struct regs *);

static const char *exception_names[32] = {
    "divide error", "debug", "NMI", "breakpoint",
    "overflow", "bound range", "invalid opcode", "device not available",
    "double fault", "coprocessor overrun", "invalid TSS", "segment not present",
    "stack fault", "general protection fault", "page fault", "reserved",
    "x87 FP error", "alignment check", "machine check", "SIMD FP error",
    "virtualization", "control protection", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved",
    "hypervisor", "VMM communication", "security", "reserved",
};

static void set_gate(int vec, void *handler, u8 dpl, u8 ist)
{
    u64 addr = (u64)handler;
    idt[vec].off_lo = addr & 0xFFFF;
    idt[vec].selector = SEG_KCODE;
    idt[vec].ist = ist & 7;
    idt[vec].flags = 0x8E | (dpl << 5);
    idt[vec].off_mid = (addr >> 16) & 0xFFFF;
    idt[vec].off_hi = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero = 0;
}

void irq_register(u8 irq, void (*fn)(struct regs *))
{
    if (irq < 16)
        irq_handlers[irq] = fn;
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));
    for (int i = 0; i < 48; i++)
        set_gate(i, (void *)isr_stub_table[i], 0, i == 8 ? 1 : 0);

    /* System call gate, callable from ring 3. */
    extern void isr128(void);
    set_gate(0x80, (void *)isr128, 3, 0);

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u64)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void exception_dump(struct regs *r)
{
    u64 cr2 = 0, cr3 = 0;
    cr2 = read_cr2();
    cr3 = read_cr3();
    kprintf("\n--- NOVA exception %lu (%s) err=%#lx ---\n",
            r->vector, r->vector < 32 ? exception_names[r->vector] : "?",
            r->error);
    kprintf("rip=%#lx cs=%#lx rflags=%#lx rsp=%#lx ss=%#lx\n",
            r->rip, r->cs, r->rflags, r->rsp, r->ss);
    kprintf("rax=%#lx rbx=%#lx rcx=%#lx rdx=%#lx\n", r->rax, r->rbx, r->rcx, r->rdx);
    kprintf("rsi=%#lx rdi=%#lx rbp=%#lx cr2=%#lx cr3=%#lx\n",
            r->rsi, r->rdi, r->rbp, cr2, cr3);
}

struct regs *trap_dispatch(struct regs *r)
{
    u8 vec = (u8)r->vector;

    if (vec == 0x80) {
        extern u64 syscall_dispatch(struct regs *r);
        r->rax = syscall_dispatch(r);
        if (syscall_need_resched())
            return schedule_from_trap(r);
        return r;
    }

    if (vec < 32) {
        extern void exception_handle(struct regs *r);
        exception_handle(r);
        return r;
    }

    if (vec >= 32 && vec < 48) {
        u8 irq = vec - 32;
        if (irq >= 8)
            outb(0xA0, 0x20);
        outb(0x20, 0x20);
        if (irq_handlers[irq])
            irq_handlers[irq](r);
        if (irq_handlers[irq] == 0 && irq != 2 && irq != 7) {
            /* spurious / unhandled: stay quiet, interrupts are masked */
        }
        if (need_resched())
            return schedule_from_trap(r);
        return r;
    }

    kprintf("unexpected interrupt vector %u\n", vec);
    return r;
}
