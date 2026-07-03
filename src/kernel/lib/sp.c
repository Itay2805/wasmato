#include "lib/assert.h"
#include "lib/defs.h"

#include "random.h"

#include <stddef.h>
#include <stdint.h>

void* __stack_chk_guard = NULL;

INIT_CODE OMIT_SP void init_stack_protector(void) {
    uint64_t rng;
    boot_random_fill(&rng, sizeof(rng));
    rng &= ~(uint64_t)0xFF;
    __stack_chk_guard = (void*)(uintptr_t)rng;
}

void __stack_chk_fail(void) {
    ASSERT(!"Stack check failed!");
}
