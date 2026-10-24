/* NOVA OS — GDT, TSS and per-CPU state. */
#include <nova/types.h>
#include <nova/io.h>
#include <nova/trap.h>
#include <nova/log.h>
#include <nova/string.h>

struct gdt_ptr {
    u16 limit;
    u64 base;
} packed;

struct tss64 {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} packed;

_Static_assert(sizeof(struct tss64) == 104, "TSS must be 104 bytes");

static u64 gdt[7] aligned(16);
static struct gdt_ptr gdtr;
static struct tss64 tss aligned(16);
static u8 double_fault_stack[8192] aligned(16);

static u64 make_tss_desc(u64 base, u32 limit)
{
    u64 d = 0;
    d |= (u64)(limit & 0xFFFF);
    d |= (base & 0xFFFFFFULL) << 16;
    d |= 0x89ULL << 40;                    /* present, type = 64-bit TSS */
    d |= (u64)((limit >> 16) & 0xF) << 48;
    d |= ((base >> 24) & 0xFFULL) << 56;
    return d;
}

void gdt_init(void)
{
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;  /* 0x08 kernel code, DPL0 */
    gdt[2] = 0x00CF92000000FFFFULL;  /* 0x10 kernel data, DPL0 */
    gdt[3] = 0x00CFF2000000FFFFULL;  /* 0x18 user data,   DPL3 */
    gdt[4] = 0x00AFFA000000FFFFULL;  /* 0x20 user code,   DPL3 */

    memset(&tss, 0, sizeof(tss));
    tss.iomap_base = sizeof(tss);    /* no I/O bitmap: ring 3 cannot do port I/O */
    tss.ist[0] = (u64)double_fault_stack + sizeof(double_fault_stack);

    u64 base = (u64)&tss;
    gdt[5] = make_tss_desc(base, sizeof(tss) - 1);
    gdt[6] = base >> 32;

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)gdt;
    gdt_load(&gdtr, SEG_KCODE, SEG_KDATA);
    tss_load(SEG_TSS);
}

void tss_set_rsp0(u64 rsp)
{
    tss.rsp0 = rsp;
}

u64 tss_get_rsp0(void)
{
    return tss.rsp0;
}
