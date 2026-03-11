#include <stdatomic.h>

#include "sched.h"
#include "timer.h"
#include "arch/intrin.h"
#include "lib/tsc.h"
#include "lib/log.h"
#include "uapi/syscall.h"
#include "uapi/entry.h"

// This is used by the tsc header, initialize it in here
uint64_t g_tsc_freq_hz;

static void hello_world(timer_t* timer) {
    TRACE("TIMER");

    timer_set_timeout(timer, 1000);
}

__attribute__((force_align_arg_pointer, nocf_check))
int _start(runtime_params_t* params) {
    static atomic_bool sched_ready = false;
    static atomic_int cpu_count = 0;

    // wait for all cores to enter the runtime before we continue
    TRACE("runtime: Entered on CPU #%d", params->cpu_id);
    cpu_count++;
    while (cpu_count != params->cpu_count) {
        cpu_relax();
    }

    if (params->cpu_id != 0) {
        // now on secondary cpus wait until we are
        // done with the init sequence and the scheduler
        // is ready to be activated
        while (!sched_ready) {
            cpu_relax();
        }

        TRACE("TODO: startup scheduler on core %d", params->cpu_id);

    } else {
        // setup the tsc freq so we can access it
        g_tsc_freq_hz = params->tsc_freq;

        // save the cpu count
        g_cpu_count = params->cpu_count;

        // setup the timer subsystem
        init_timers(params->timer_vector);

        // TODO: setup the scheduler

        // TODO: setup interrupt handling

        // we are done, let the kernel know we won't
        // need anything else
        sys_early_done();

        // let the other cores know that we are
        // ready to run
        sched_ready = true;

        static timer_t timer = {
            .callback = hello_world
        };
        timer_set_timeout(&timer, 1000);

        // TODO: startup scheduler
        TRACE("TODO: startup scheduler on core %d", params->cpu_id);
    }

    // should not reach here
    irq_enable();
    while (1) {
        cpu_relax();
    }
}
