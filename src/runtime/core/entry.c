#include <stdatomic.h>

#include "proc/thread.h"
#include "sched.h"
#include "timer.h"
#include "arch/intrin.h"
#include "lib/tsc.h"
#include "lib/log.h"
#include "uapi/syscall.h"
#include "uapi/entry.h"

// This is used by the tsc header, initialize it in here
uint64_t g_tsc_freq_hz;

static atomic_bool m_sched_ready = false;
static atomic_int m_cpu_count = 0;

// the actual main function
void main(void* arg);

__attribute__((force_align_arg_pointer, nocf_check))
int _start(runtime_params_t* params) {
    // must not be enabled at this point
    ASSERT(!is_irq_enabled());

    // wait for all cores to enter the runtime before we continue
    m_cpu_count++;
    while (m_cpu_count != params->cpu_count) {
        cpu_relax();
    }

    if (params->cpu_id != 0) {
        // now on secondary cpus wait until we are
        // done with the init sequence and the scheduler
        // is ready to be activated
        while (!m_sched_ready) {
            cpu_relax();
        }

    } else {
        // setup the tsc freq so we can access it
        g_tsc_freq_hz = params->tsc_freq;

        // save the cpu count
        g_cpu_count = params->cpu_count;

        // setup the timer subsystem
        init_timers(params->timer_vector);

        // setup threading
        init_threads(params->tls_size);

        // setup scheduler
        init_sched();

        // create the init thread and start it
        thread_t* thread = thread_create(main, nullptr, "init");
        ASSERT(thread != nullptr);
        thread_start(thread);

        // let the other cores know that we are
        // ready to run
        m_sched_ready = true;
    }

    // we can start the scheduler on all cores
    sched_start_per_core();
}
