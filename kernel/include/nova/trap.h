/* NOVA OS — interrupt/trap definitions shared by the assembly stubs. */
#ifndef NOVA_TRAP_H
#define NOVA_TRAP_H

#include <nova/types.h>

/* Layout must match the push order in arch/x86_64/isr.S. */
struct regs {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector, error;
    u64 rip, cs, rflags, rsp, ss;
};

_Static_assert(sizeof(struct regs) == 176, "regs layout drift");

#define SEG_KCODE 0x08
#define SEG_KDATA 0x10
#define SEG_UDATA 0x18
#define SEG_UCODE 0x20
#define SEG_TSS   0x28

#define SEG_UDATA_RPL3 (SEG_UDATA | 3)
#define SEG_UCODE_RPL3 (SEG_UCODE | 3)

#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_MOUSE    12
#define IRQ_ATA      14

void gdt_init(void);
void idt_init(void);
void pic_init(void);
void pic_mask(u8 irq, bool masked);
void pic_eoi(u8 irq);
void pit_init(u32 hz);
u64  pit_ticks(void);
u32  pit_hz(void);

struct regs *trap_dispatch(struct regs *r);
void irq_register(u8 irq, void (*fn)(struct regs *));
struct regs *schedule_from_trap(struct regs *r);
bool need_resched(void);
bool syscall_need_resched(void);
void set_need_resched(void);
void tss_set_rsp0(u64 rsp);
u64  tss_get_rsp0(void);
void exception_dump(struct regs *r);

/* Provided by isr.S */
extern void isr_table(void);
extern u64 isr_stub_table[48];

#endif /* NOVA_TRAP_H */
