/* NOVA OS — 8254 programmable interval timer (system tick). */
#include <nova/types.h>
#include <nova/io.h>
#include <nova/trap.h>

#define PIT_CH0  0x40
#define PIT_CMD  0x43
#define PIT_BASE 1193182u

static u64 ticks;
static u32 tick_hz = 100;

static void pit_irq(struct regs *r)
{
    (void)r;
    ticks++;
    extern void sched_tick(void);
    sched_tick();
    if ((ticks % tick_hz) == 0)
        set_need_resched();
}

void pit_init(u32 hz)
{
    if (hz == 0)
        hz = 100;
    tick_hz = hz;
    u32 divisor = PIT_BASE / hz;
    if (divisor == 0)
        divisor = 1;
    if (divisor > 0xFFFF)
        divisor = 0xFFFF;

    outb(PIT_CMD, 0x36);                 /* channel 0, lobyte/hibyte, mode 3 */
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
    ticks = 0;

    irq_register(IRQ_TIMER, pit_irq);
    pic_mask(IRQ_TIMER, false);
}

u64 pit_ticks(void) { return ticks; }
u32 pit_hz(void) { return tick_hz; }
