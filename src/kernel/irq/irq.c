#include "irq.h"

#include "ioapic.h"

#include "arch/apic.h"
#include "arch/intr.h"
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/pcpu.h"
#include "mem/alloc.h"
#include "sync/spinlock.h"
#include "thread/sched.h"
#include "thread/wait.h"
#include "uapi/irq.h"
#include "user/object.h"

typedef struct irq_dispatcher {
    /**
     * The dispatch table
     */
    irq_t* table[INTR_VECTOR_LAST - INTR_VECTOR_FIRST];

    /**
     * The interrupt table lock
     */
    irq_spinlock_t lock;
} irq_dispatcher_t;

/**
 * The IRQ dispatcher of the current core
 */
static irq_dispatcher_t CPU_LOCAL m_irq_dispatcher;

static mem_alloc_t m_irq_alloc;

static irq_dispatcher_t* get_irq_dispatcher(void) {
    return pcpu_get_pointer(&m_irq_dispatcher);
}

static irq_dispatcher_t* get_irq_dispatcher_of(int cpu_id) {
    return pcpu_get_pointer_of(&m_irq_dispatcher, cpu_id);
}

static void irq_mask(irq_t* irq) {
    if (irq->type == IRQ_TYPE_IOAPIC) {
        ioapic_set_mask(irq, true);
    } else {
        ASSERT(!"Invalid interrupt type for mask");
    }
}

void interrupt_unmask(irq_t* irq) {
    if (irq->type == IRQ_TYPE_IOAPIC) {
        ioapic_set_mask(irq, false);
    } else {
        ASSERT(!"Invalid interrupt type for unmask");
    }    
}

irq_t* irq_create(irq_waiter_t* waiter, int cpu_id) {
    irq_t* irq = mem_alloc(&m_irq_alloc);
    if (irq == nullptr) {
        return nullptr;
    }

    // setup the object
    irq->object.type = KERNEL_OBJECT_TYPE_IRQ;
    irq->object.ref_count = 1;

    irq->type = IRQ_TYPE_UNREGISTERED;
    irq->cpu_id = cpu_id;
    irq->waiter = waiter;

    // acquire the spinlock of the given cpu
    irq_dispatcher_t* dispatcher = get_irq_dispatcher_of(cpu_id);
    bool irq_state = irq_spinlock_acquire(&dispatcher->lock);

    // get its interrupt table and search for an unused entry
    int idx = -1;
    for (int i = 0; i < ARRAY_LENGTH(dispatcher->table); i++) {
        if (dispatcher->table[i] == nullptr) {
            idx = i;
            break;
        }
    }

    // if not found then fail
    if (idx != -1) {
        irq_spinlock_release(&dispatcher->lock, irq_state);
        mem_free(&m_irq_alloc, irq);
        return nullptr;
    }

    // save it and return
    irq->vector = INTR_VECTOR_FIRST + idx;
    dispatcher->table[idx] = irq;

    irq_spinlock_release(&dispatcher->lock, irq_state);

    return irq;
}

void irq_free(irq_t* irq) {
    // acquire the spinlock of the given cpu
    irq_dispatcher_t* dispatcher = get_irq_dispatcher_of(irq->cpu_id);
    bool irq_state = irq_spinlock_acquire(&dispatcher->lock);
    dispatcher->table[irq->vector - INTR_VECTOR_FIRST] = nullptr;
    irq_mask(irq);
    irq_spinlock_release(&dispatcher->lock, irq_state);

    // And now wakeup the waiters so they can know the irq is dead
    atomic_store_release(irq->waiter, IRQ_WAITER_DEAD);
    atomic_notify(irq->waiter, 0);
    
    // and we can free it
    mem_free(&m_irq_alloc, irq);
}

static void irq_dispatch(uint8_t index) {
    irq_dispatcher_t* dispatcher = get_irq_dispatcher();
    bool irq_state = irq_spinlock_acquire(&dispatcher->lock);

    irq_t* irq = dispatcher->table[index - INTR_VECTOR_FIRST];
    if (irq != nullptr) {
        // mask the interrupt so it won't fire again
        irq_mask(irq);

        // And now wakeup the waiters so they can handle it
        atomic_store_release(irq->waiter, IRQ_WAITER_WAKEUP);
        atomic_notify(irq->waiter, 0);

    } else {
        WARN("irq: got #%d on cpu #%d with no handler attached", index, get_cpu_id());
    }

    irq_spinlock_release(&dispatcher->lock, irq_state);

    // we can ack the interrupt now
    lapic_eoi();
}

#define IRQ_STUB(num) \
    STATIC_ASSERT(INTR_VECTOR_FIRST <= num && num <= INTR_VECTOR_LAST); \
    __attribute__((interrupt)) \
    static void irq_handler_##num(interrupt_frame_t* frame) { \
        irq_dispatch(num); \
    }

IRQ_STUB(0x21)
IRQ_STUB(0x22)
IRQ_STUB(0x23)
IRQ_STUB(0x24)
IRQ_STUB(0x25)
IRQ_STUB(0x26)
IRQ_STUB(0x27)
IRQ_STUB(0x28)
IRQ_STUB(0x29)
IRQ_STUB(0x2a)
IRQ_STUB(0x2b)
IRQ_STUB(0x2c)
IRQ_STUB(0x2d)
IRQ_STUB(0x2e)
IRQ_STUB(0x2f)
IRQ_STUB(0x30)
IRQ_STUB(0x31)
IRQ_STUB(0x32)
IRQ_STUB(0x33)
IRQ_STUB(0x34)
IRQ_STUB(0x35)
IRQ_STUB(0x36)
IRQ_STUB(0x37)
IRQ_STUB(0x38)
IRQ_STUB(0x39)
IRQ_STUB(0x3a)
IRQ_STUB(0x3b)
IRQ_STUB(0x3c)
IRQ_STUB(0x3d)
IRQ_STUB(0x3e)
IRQ_STUB(0x3f)
IRQ_STUB(0x40)
IRQ_STUB(0x41)
IRQ_STUB(0x42)
IRQ_STUB(0x43)
IRQ_STUB(0x44)
IRQ_STUB(0x45)
IRQ_STUB(0x46)
IRQ_STUB(0x47)
IRQ_STUB(0x48)
IRQ_STUB(0x49)
IRQ_STUB(0x4a)
IRQ_STUB(0x4b)
IRQ_STUB(0x4c)
IRQ_STUB(0x4d)
IRQ_STUB(0x4e)
IRQ_STUB(0x4f)
IRQ_STUB(0x50)
IRQ_STUB(0x51)
IRQ_STUB(0x52)
IRQ_STUB(0x53)
IRQ_STUB(0x54)
IRQ_STUB(0x55)
IRQ_STUB(0x56)
IRQ_STUB(0x57)
IRQ_STUB(0x58)
IRQ_STUB(0x59)
IRQ_STUB(0x5a)
IRQ_STUB(0x5b)
IRQ_STUB(0x5c)
IRQ_STUB(0x5d)
IRQ_STUB(0x5e)
IRQ_STUB(0x5f)
IRQ_STUB(0x60)
IRQ_STUB(0x61)
IRQ_STUB(0x62)
IRQ_STUB(0x63)
IRQ_STUB(0x64)
IRQ_STUB(0x65)
IRQ_STUB(0x66)
IRQ_STUB(0x67)
IRQ_STUB(0x68)
IRQ_STUB(0x69)
IRQ_STUB(0x6a)
IRQ_STUB(0x6b)
IRQ_STUB(0x6c)
IRQ_STUB(0x6d)
IRQ_STUB(0x6e)
IRQ_STUB(0x6f)
IRQ_STUB(0x70)
IRQ_STUB(0x71)
IRQ_STUB(0x72)
IRQ_STUB(0x73)
IRQ_STUB(0x74)
IRQ_STUB(0x75)
IRQ_STUB(0x76)
IRQ_STUB(0x77)
IRQ_STUB(0x78)
IRQ_STUB(0x79)
IRQ_STUB(0x7a)
IRQ_STUB(0x7b)
IRQ_STUB(0x7c)
IRQ_STUB(0x7d)
IRQ_STUB(0x7e)
IRQ_STUB(0x7f)
IRQ_STUB(0x80)
IRQ_STUB(0x81)
IRQ_STUB(0x82)
IRQ_STUB(0x83)
IRQ_STUB(0x84)
IRQ_STUB(0x85)
IRQ_STUB(0x86)
IRQ_STUB(0x87)
IRQ_STUB(0x88)
IRQ_STUB(0x89)
IRQ_STUB(0x8a)
IRQ_STUB(0x8b)
IRQ_STUB(0x8c)
IRQ_STUB(0x8d)
IRQ_STUB(0x8e)
IRQ_STUB(0x8f)
IRQ_STUB(0x90)
IRQ_STUB(0x91)
IRQ_STUB(0x92)
IRQ_STUB(0x93)
IRQ_STUB(0x94)
IRQ_STUB(0x95)
IRQ_STUB(0x96)
IRQ_STUB(0x97)
IRQ_STUB(0x98)
IRQ_STUB(0x99)
IRQ_STUB(0x9a)
IRQ_STUB(0x9b)
IRQ_STUB(0x9c)
IRQ_STUB(0x9d)
IRQ_STUB(0x9e)
IRQ_STUB(0x9f)
IRQ_STUB(0xa0)
IRQ_STUB(0xa1)
IRQ_STUB(0xa2)
IRQ_STUB(0xa3)
IRQ_STUB(0xa4)
IRQ_STUB(0xa5)
IRQ_STUB(0xa6)
IRQ_STUB(0xa7)
IRQ_STUB(0xa8)
IRQ_STUB(0xa9)
IRQ_STUB(0xaa)
IRQ_STUB(0xab)
IRQ_STUB(0xac)
IRQ_STUB(0xad)
IRQ_STUB(0xae)
IRQ_STUB(0xaf)
IRQ_STUB(0xb0)
IRQ_STUB(0xb1)
IRQ_STUB(0xb2)
IRQ_STUB(0xb3)
IRQ_STUB(0xb4)
IRQ_STUB(0xb5)
IRQ_STUB(0xb6)
IRQ_STUB(0xb7)
IRQ_STUB(0xb8)
IRQ_STUB(0xb9)
IRQ_STUB(0xba)
IRQ_STUB(0xbb)
IRQ_STUB(0xbc)
IRQ_STUB(0xbd)
IRQ_STUB(0xbe)
IRQ_STUB(0xbf)
IRQ_STUB(0xc0)
IRQ_STUB(0xc1)
IRQ_STUB(0xc2)
IRQ_STUB(0xc3)
IRQ_STUB(0xc4)
IRQ_STUB(0xc5)
IRQ_STUB(0xc6)
IRQ_STUB(0xc7)
IRQ_STUB(0xc8)
IRQ_STUB(0xc9)
IRQ_STUB(0xca)
IRQ_STUB(0xcb)
IRQ_STUB(0xcc)
IRQ_STUB(0xcd)
IRQ_STUB(0xce)
IRQ_STUB(0xcf)
IRQ_STUB(0xd0)
IRQ_STUB(0xd1)
IRQ_STUB(0xd2)
IRQ_STUB(0xd3)
IRQ_STUB(0xd4)
IRQ_STUB(0xd5)
IRQ_STUB(0xd6)
IRQ_STUB(0xd7)
IRQ_STUB(0xd8)
IRQ_STUB(0xd9)
IRQ_STUB(0xda)
IRQ_STUB(0xdb)
IRQ_STUB(0xdc)
IRQ_STUB(0xdd)
IRQ_STUB(0xde)
IRQ_STUB(0xdf)
IRQ_STUB(0xe0)
IRQ_STUB(0xe1)
IRQ_STUB(0xe2)
IRQ_STUB(0xe3)
IRQ_STUB(0xe4)
IRQ_STUB(0xe5)
IRQ_STUB(0xe6)
IRQ_STUB(0xe7)
IRQ_STUB(0xe8)
IRQ_STUB(0xe9)
IRQ_STUB(0xea)
IRQ_STUB(0xeb)
IRQ_STUB(0xec)
IRQ_STUB(0xed)
IRQ_STUB(0xee)
IRQ_STUB(0xef)

INIT_CODE void init_irq_handling(void) {
    intr_set_handler(0x21, irq_handler_0x21);
    intr_set_handler(0x22, irq_handler_0x22);
    intr_set_handler(0x23, irq_handler_0x23);
    intr_set_handler(0x24, irq_handler_0x24);
    intr_set_handler(0x25, irq_handler_0x25);
    intr_set_handler(0x26, irq_handler_0x26);
    intr_set_handler(0x27, irq_handler_0x27);
    intr_set_handler(0x28, irq_handler_0x28);
    intr_set_handler(0x29, irq_handler_0x29);
    intr_set_handler(0x2a, irq_handler_0x2a);
    intr_set_handler(0x2b, irq_handler_0x2b);
    intr_set_handler(0x2c, irq_handler_0x2c);
    intr_set_handler(0x2d, irq_handler_0x2d);
    intr_set_handler(0x2e, irq_handler_0x2e);
    intr_set_handler(0x2f, irq_handler_0x2f);
    intr_set_handler(0x30, irq_handler_0x30);
    intr_set_handler(0x31, irq_handler_0x31);
    intr_set_handler(0x32, irq_handler_0x32);
    intr_set_handler(0x33, irq_handler_0x33);
    intr_set_handler(0x34, irq_handler_0x34);
    intr_set_handler(0x35, irq_handler_0x35);
    intr_set_handler(0x36, irq_handler_0x36);
    intr_set_handler(0x37, irq_handler_0x37);
    intr_set_handler(0x38, irq_handler_0x38);
    intr_set_handler(0x39, irq_handler_0x39);
    intr_set_handler(0x3a, irq_handler_0x3a);
    intr_set_handler(0x3b, irq_handler_0x3b);
    intr_set_handler(0x3c, irq_handler_0x3c);
    intr_set_handler(0x3d, irq_handler_0x3d);
    intr_set_handler(0x3e, irq_handler_0x3e);
    intr_set_handler(0x3f, irq_handler_0x3f);
    intr_set_handler(0x40, irq_handler_0x40);
    intr_set_handler(0x41, irq_handler_0x41);
    intr_set_handler(0x42, irq_handler_0x42);
    intr_set_handler(0x43, irq_handler_0x43);
    intr_set_handler(0x44, irq_handler_0x44);
    intr_set_handler(0x45, irq_handler_0x45);
    intr_set_handler(0x46, irq_handler_0x46);
    intr_set_handler(0x47, irq_handler_0x47);
    intr_set_handler(0x48, irq_handler_0x48);
    intr_set_handler(0x49, irq_handler_0x49);
    intr_set_handler(0x4a, irq_handler_0x4a);
    intr_set_handler(0x4b, irq_handler_0x4b);
    intr_set_handler(0x4c, irq_handler_0x4c);
    intr_set_handler(0x4d, irq_handler_0x4d);
    intr_set_handler(0x4e, irq_handler_0x4e);
    intr_set_handler(0x4f, irq_handler_0x4f);
    intr_set_handler(0x50, irq_handler_0x50);
    intr_set_handler(0x51, irq_handler_0x51);
    intr_set_handler(0x52, irq_handler_0x52);
    intr_set_handler(0x53, irq_handler_0x53);
    intr_set_handler(0x54, irq_handler_0x54);
    intr_set_handler(0x55, irq_handler_0x55);
    intr_set_handler(0x56, irq_handler_0x56);
    intr_set_handler(0x57, irq_handler_0x57);
    intr_set_handler(0x58, irq_handler_0x58);
    intr_set_handler(0x59, irq_handler_0x59);
    intr_set_handler(0x5a, irq_handler_0x5a);
    intr_set_handler(0x5b, irq_handler_0x5b);
    intr_set_handler(0x5c, irq_handler_0x5c);
    intr_set_handler(0x5d, irq_handler_0x5d);
    intr_set_handler(0x5e, irq_handler_0x5e);
    intr_set_handler(0x5f, irq_handler_0x5f);
    intr_set_handler(0x60, irq_handler_0x60);
    intr_set_handler(0x61, irq_handler_0x61);
    intr_set_handler(0x62, irq_handler_0x62);
    intr_set_handler(0x63, irq_handler_0x63);
    intr_set_handler(0x64, irq_handler_0x64);
    intr_set_handler(0x65, irq_handler_0x65);
    intr_set_handler(0x66, irq_handler_0x66);
    intr_set_handler(0x67, irq_handler_0x67);
    intr_set_handler(0x68, irq_handler_0x68);
    intr_set_handler(0x69, irq_handler_0x69);
    intr_set_handler(0x6a, irq_handler_0x6a);
    intr_set_handler(0x6b, irq_handler_0x6b);
    intr_set_handler(0x6c, irq_handler_0x6c);
    intr_set_handler(0x6d, irq_handler_0x6d);
    intr_set_handler(0x6e, irq_handler_0x6e);
    intr_set_handler(0x6f, irq_handler_0x6f);
    intr_set_handler(0x70, irq_handler_0x70);
    intr_set_handler(0x71, irq_handler_0x71);
    intr_set_handler(0x72, irq_handler_0x72);
    intr_set_handler(0x73, irq_handler_0x73);
    intr_set_handler(0x74, irq_handler_0x74);
    intr_set_handler(0x75, irq_handler_0x75);
    intr_set_handler(0x76, irq_handler_0x76);
    intr_set_handler(0x77, irq_handler_0x77);
    intr_set_handler(0x78, irq_handler_0x78);
    intr_set_handler(0x79, irq_handler_0x79);
    intr_set_handler(0x7a, irq_handler_0x7a);
    intr_set_handler(0x7b, irq_handler_0x7b);
    intr_set_handler(0x7c, irq_handler_0x7c);
    intr_set_handler(0x7d, irq_handler_0x7d);
    intr_set_handler(0x7e, irq_handler_0x7e);
    intr_set_handler(0x7f, irq_handler_0x7f);
    intr_set_handler(0x80, irq_handler_0x80);
    intr_set_handler(0x81, irq_handler_0x81);
    intr_set_handler(0x82, irq_handler_0x82);
    intr_set_handler(0x83, irq_handler_0x83);
    intr_set_handler(0x84, irq_handler_0x84);
    intr_set_handler(0x85, irq_handler_0x85);
    intr_set_handler(0x86, irq_handler_0x86);
    intr_set_handler(0x87, irq_handler_0x87);
    intr_set_handler(0x88, irq_handler_0x88);
    intr_set_handler(0x89, irq_handler_0x89);
    intr_set_handler(0x8a, irq_handler_0x8a);
    intr_set_handler(0x8b, irq_handler_0x8b);
    intr_set_handler(0x8c, irq_handler_0x8c);
    intr_set_handler(0x8d, irq_handler_0x8d);
    intr_set_handler(0x8e, irq_handler_0x8e);
    intr_set_handler(0x8f, irq_handler_0x8f);
    intr_set_handler(0x90, irq_handler_0x90);
    intr_set_handler(0x91, irq_handler_0x91);
    intr_set_handler(0x92, irq_handler_0x92);
    intr_set_handler(0x93, irq_handler_0x93);
    intr_set_handler(0x94, irq_handler_0x94);
    intr_set_handler(0x95, irq_handler_0x95);
    intr_set_handler(0x96, irq_handler_0x96);
    intr_set_handler(0x97, irq_handler_0x97);
    intr_set_handler(0x98, irq_handler_0x98);
    intr_set_handler(0x99, irq_handler_0x99);
    intr_set_handler(0x9a, irq_handler_0x9a);
    intr_set_handler(0x9b, irq_handler_0x9b);
    intr_set_handler(0x9c, irq_handler_0x9c);
    intr_set_handler(0x9d, irq_handler_0x9d);
    intr_set_handler(0x9e, irq_handler_0x9e);
    intr_set_handler(0x9f, irq_handler_0x9f);
    intr_set_handler(0xa0, irq_handler_0xa0);
    intr_set_handler(0xa1, irq_handler_0xa1);
    intr_set_handler(0xa2, irq_handler_0xa2);
    intr_set_handler(0xa3, irq_handler_0xa3);
    intr_set_handler(0xa4, irq_handler_0xa4);
    intr_set_handler(0xa5, irq_handler_0xa5);
    intr_set_handler(0xa6, irq_handler_0xa6);
    intr_set_handler(0xa7, irq_handler_0xa7);
    intr_set_handler(0xa8, irq_handler_0xa8);
    intr_set_handler(0xa9, irq_handler_0xa9);
    intr_set_handler(0xaa, irq_handler_0xaa);
    intr_set_handler(0xab, irq_handler_0xab);
    intr_set_handler(0xac, irq_handler_0xac);
    intr_set_handler(0xad, irq_handler_0xad);
    intr_set_handler(0xae, irq_handler_0xae);
    intr_set_handler(0xaf, irq_handler_0xaf);
    intr_set_handler(0xb0, irq_handler_0xb0);
    intr_set_handler(0xb1, irq_handler_0xb1);
    intr_set_handler(0xb2, irq_handler_0xb2);
    intr_set_handler(0xb3, irq_handler_0xb3);
    intr_set_handler(0xb4, irq_handler_0xb4);
    intr_set_handler(0xb5, irq_handler_0xb5);
    intr_set_handler(0xb6, irq_handler_0xb6);
    intr_set_handler(0xb7, irq_handler_0xb7);
    intr_set_handler(0xb8, irq_handler_0xb8);
    intr_set_handler(0xb9, irq_handler_0xb9);
    intr_set_handler(0xba, irq_handler_0xba);
    intr_set_handler(0xbb, irq_handler_0xbb);
    intr_set_handler(0xbc, irq_handler_0xbc);
    intr_set_handler(0xbd, irq_handler_0xbd);
    intr_set_handler(0xbe, irq_handler_0xbe);
    intr_set_handler(0xbf, irq_handler_0xbf);
    intr_set_handler(0xc0, irq_handler_0xc0);
    intr_set_handler(0xc1, irq_handler_0xc1);
    intr_set_handler(0xc2, irq_handler_0xc2);
    intr_set_handler(0xc3, irq_handler_0xc3);
    intr_set_handler(0xc4, irq_handler_0xc4);
    intr_set_handler(0xc5, irq_handler_0xc5);
    intr_set_handler(0xc6, irq_handler_0xc6);
    intr_set_handler(0xc7, irq_handler_0xc7);
    intr_set_handler(0xc8, irq_handler_0xc8);
    intr_set_handler(0xc9, irq_handler_0xc9);
    intr_set_handler(0xca, irq_handler_0xca);
    intr_set_handler(0xcb, irq_handler_0xcb);
    intr_set_handler(0xcc, irq_handler_0xcc);
    intr_set_handler(0xcd, irq_handler_0xcd);
    intr_set_handler(0xce, irq_handler_0xce);
    intr_set_handler(0xcf, irq_handler_0xcf);
    intr_set_handler(0xd0, irq_handler_0xd0);
    intr_set_handler(0xd1, irq_handler_0xd1);
    intr_set_handler(0xd2, irq_handler_0xd2);
    intr_set_handler(0xd3, irq_handler_0xd3);
    intr_set_handler(0xd4, irq_handler_0xd4);
    intr_set_handler(0xd5, irq_handler_0xd5);
    intr_set_handler(0xd6, irq_handler_0xd6);
    intr_set_handler(0xd7, irq_handler_0xd7);
    intr_set_handler(0xd8, irq_handler_0xd8);
    intr_set_handler(0xd9, irq_handler_0xd9);
    intr_set_handler(0xda, irq_handler_0xda);
    intr_set_handler(0xdb, irq_handler_0xdb);
    intr_set_handler(0xdc, irq_handler_0xdc);
    intr_set_handler(0xdd, irq_handler_0xdd);
    intr_set_handler(0xde, irq_handler_0xde);
    intr_set_handler(0xdf, irq_handler_0xdf);
    intr_set_handler(0xe0, irq_handler_0xe0);
    intr_set_handler(0xe1, irq_handler_0xe1);
    intr_set_handler(0xe2, irq_handler_0xe2);
    intr_set_handler(0xe3, irq_handler_0xe3);
    intr_set_handler(0xe4, irq_handler_0xe4);
    intr_set_handler(0xe5, irq_handler_0xe5);
    intr_set_handler(0xe6, irq_handler_0xe6);
    intr_set_handler(0xe7, irq_handler_0xe7);
    intr_set_handler(0xe8, irq_handler_0xe8);
    intr_set_handler(0xe9, irq_handler_0xe9);
    intr_set_handler(0xea, irq_handler_0xea);
    intr_set_handler(0xeb, irq_handler_0xeb);
    intr_set_handler(0xec, irq_handler_0xec);
    intr_set_handler(0xed, irq_handler_0xed);
    intr_set_handler(0xee, irq_handler_0xee);
    intr_set_handler(0xef, irq_handler_0xef);

    // setup the allocator
    mem_alloc_init(&m_irq_alloc, sizeof(irq_t), _Alignof(irq_t));
}
