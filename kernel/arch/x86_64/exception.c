/* NOVA OS — synchronous exceptions.
 *
 * Kernel faults are fatal and print a register dump.  Ring-3 faults terminate
 * the offending process (the subsystem reports the NTSTATUS-style code), which
 * is what a hardened service expects from bad user code.
 */
#include <nova/types.h>
#include <nova/trap.h>
#include <nova/sched.h>
#include <nova/log.h>
#include <nova/io.h>
#include <nova/string.h>
#include <nova/mm.h>
#include <nova/console.h>

void exception_handle(struct regs *r)
{
    bool user = (r->cs & 3) == 3;

    if (user) {
        struct process *p = process_current();
        u64 cr2 = r->vector == 14 ? read_cr2() : 0;
        kprintf("[fault] pid %d '%s' vector %lu at rip=%#lx%s%#lx\n",
                p ? p->pid : -1, p ? p->name : "?",
                r->vector, r->rip,
                r->vector == 14 ? " addr=" : "", cr2);

        /* Report the failure into the process console so the user sees it. */
        char msg[192];
        ksnprintf(msg, sizeof(msg),
                  "\n[process terminated: %s fault at %#lx]\n",
                  r->vector < 32 ? "CPU" : "unknown", r->rip);
        console_write_process(p, msg, strlen(msg));

        if (p) {
            process_terminate(p, (int)(0xC0000000u | (r->vector & 0xFF)));
            set_need_resched();
        } else {
            thread_exit();
        }
        return;
    }

    exception_dump(r);
    panic("unhandled kernel exception %lu", r->vector);
}
