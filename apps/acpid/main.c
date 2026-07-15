#include "trace.h"
#include "uacpi/status.h"
#include <stdint.h>
#include <time.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>

#include <stdio.h>
#include <unistd.h>

static uacpi_interrupt_ret handle_power_button_fixed(uacpi_handle ctx) {
    TRACE("power button pressed!");
    return UACPI_INTERRUPT_HANDLED;
}

int main(void) {
    /*
     * Start with this as the first step of the initialization. This loads all
     * tables, brings the event subsystem online, and enters ACPI mode. We pass
     * in 0 as the flags as we don't want to override any default behavior for now.
     */
    uacpi_status ret = uacpi_initialize(0);
    if (uacpi_unlikely_error(ret)) {
        printf("uacpi_initialize error: %s\n", uacpi_status_to_string(ret));
        return -1;
    }

    /*
     * Load the AML namespace. This feeds DSDT and all SSDTs to the interpreter
     * for execution.
     */
    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        printf("uacpi_namespace_load error: %s\n", uacpi_status_to_string(ret));
        return -2;
    }

    /*
     * Initialize the namespace. This calls all necessary _STA/_INI AML methods,
     * as well as _REG for registered operation region handlers.
     */
    ret = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(ret)) {
        printf("uacpi_namespace_initialize error: %s\n", uacpi_status_to_string(ret));
        return -3;
    }
    
    /*
     * Tell the firmware the interrupt model we're planning to use.
     * (Use UACPI_INTERRUPT_MODEL_PIC if you're planning to use PIC, or any
     *  other value depending on the architecture).
     */
    uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);

    /*
     * Tell uACPI that we have marked all GPEs we wanted for wake (even though we haven't
     * actually marked any, as we have no power management support right now). This is
     * needed to let uACPI enable all unmarked GPEs that have a corresponding AML handler.
     * These handlers are used by the firmware to dynamically execute AML code at runtime
     * to e.g. react to thermal events or device hotplug.
     */
    ret = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely_error(ret)) {
        printf("uACPI GPE initialization error: %s\n", uacpi_status_to_string(ret));
        return -4;
    }

    /* Fixed event — harmless to install even if firmware uses the CM button;
     * it just won't fire. */
    ret = uacpi_install_fixed_event_handler(
        UACPI_FIXED_EVENT_POWER_BUTTON,
        handle_power_button_fixed,
        UACPI_NULL
    );

    /*
     * That's it, uACPI is now fully initialized and working! You can proceed to
     * using any public API at your discretion. The next recommended step is namespace
     * enumeration and device discovery so you can bind drivers to ACPI objects.
     */


    // TODO: IPC handling
    for (;;) {
        struct timespec sp = { .tv_sec = 1000 };
        clock_nanosleep(CLOCK_MONOTONIC, 0, &sp, NULL);
    }

    return 0;
}
