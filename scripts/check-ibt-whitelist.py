#!/usr/bin/env python3
"""check-ibt-whitelist.py: Verify that only expected kernel symbols have endbr64
as their first instruction (i.e., are valid IBT indirect-call targets), and that
every whitelisted symbol actually exists in the binary.

Usage: scripts/check-ibt-whitelist.py build/kernel
"""

import sys
from collections import defaultdict
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from elftools.elf.constants import SH_FLAGS

ENDBR64 = b'\xf3\x0f\x1e\xfa'

# Symbols legitimately reachable via indirect call (function pointers / IDT).
# Every symbol here must exist in the binary and start with endbr64.
# Any symbol NOT listed here that starts with endbr64 is an error.

WHITELIST: set[str] = {
    # Syscall path
    "syscall_entry",

    # Exception/interrupt assembly stubs and C handler
    "panic_handler",
    "timer_interrupt_handler",
    "ipi_interrupt_handler",
    "spurious_interrupt_handler",

    # Per-vector exception stubs (0x00-0x1F)
    # 0x0E (page fault) has its own function
    *(f"exception_handler_0x{i:02X}" for i in range(0x20)),

    # Scheduler
    "scheduler_idle_thread",
    "scheduler_tick",
    "schedule_timer_wakeup",

    # runtime startup
    "runtime_thread_entry_thunk",

    # printf character callback
    "debug_print_cb",
    "sprintf_cb",
}

WHITELIST_DEBUG = {
    # Red-black tree callbacks, inlined
    # in release
    "vmar_cmp",
    "vmar_cmp_overlap",
    "vmar_less",
    "dummy_rotate",
    "dummy_copy",
    "dummy_propagate",
    "timer_less",
}

# Non-function linker/section-marker symbols that may share an address with a
# real function.  They are silently excluded from both checks.
IGNORE: set[str] = {
    "__kernel_text_base",
    "__kernel_init_text_base",
}


def main() -> None:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <kernel-elf> [<--debug>]", file=sys.stderr)
        sys.exit(1)

    kernel = sys.argv[1]
    if len(sys.argv) > 2:
        assert sys.argv[2] == '--debug'
        WHITELIST.update(WHITELIST_DEBUG)

    with open(kernel, 'rb') as f:
        elf = ELFFile(f)

        # Build a map of PT_LOAD segments for virtual-address -> bytes lookup
        segments: list[tuple[int, int, bytes]] = []
        for seg in elf.iter_segments():
            if seg.header.p_type != 'PT_LOAD':
                continue
            vaddr = seg.header.p_vaddr
            filesz = seg.header.p_filesz
            if filesz > 0:
                segments.append((vaddr, vaddr + filesz, seg.data()))

        def read_va(addr: int, size: int) -> bytes | None:
            for vaddr, vend, data in segments:
                if vaddr <= addr < vend:
                    off = addr - vaddr
                    if off + size <= len(data):
                        return data[off:off + size]
            return None

        # build addr -> {symbol-names} from .symtab
        # Only consider symbols in executable section
        exec_shndxs: set[int] = {
            i for i, sec in enumerate(elf.iter_sections())
            if sec['sh_flags'] & SH_FLAGS.SHF_EXECINSTR
        }

        symtab = elf.get_section_by_name('.symtab')
        assert symtab is not None and isinstance(symtab, SymbolTableSection)

        addr_to_syms: dict[int, set[str]] = defaultdict(set)
        sym_to_addr: dict[str, int] = {}

        for sym in symtab.iter_symbols():
            shndx = sym['st_shndx']
            if isinstance(shndx, str):
                continue
            if shndx not in exec_shndxs:
                continue
            addr = sym['st_value']
            if addr == 0:
                continue
            name = sym.name
            addr_to_syms[addr].add(name)
            sym_to_addr[name] = addr

        # find symbol addresses whose first instruction is endbr64
        endbr_addrs: set[int] = {
            addr for addr in addr_to_syms
            if read_va(addr, 4) == ENDBR64
        }

    # derive the set of symbol names that have endbr64
    syms_with_endbr: set[str] = set()
    for addr in endbr_addrs:
        syms_with_endbr.update(addr_to_syms[addr])

    syms_with_endbr -= IGNORE
    all_text_syms: set[str] = set(sym_to_addr) - IGNORE

    errors = 0

    # Check symbols with endbr64 not in the whitelist
    unexpected = syms_with_endbr - WHITELIST
    if unexpected:
        print("ERROR: symbols with endbr64 not in whitelist (unexpected indirect-call targets):")
        for s in sorted(unexpected):
            print(f"  {s}")
        errors += 1

    # Check whitelisted symbols that exist in the binary but lack endbr64
    missing_endbr = (WHITELIST & all_text_syms) - syms_with_endbr
    if missing_endbr:
        print("ERROR: whitelisted symbols exist but have no endbr64 (not indirect-call targets?):")
        for s in sorted(missing_endbr):
            print(f"  {s}")
        errors += 1

    # Check whitelisted symbols absent from the binary entirely
    absent = WHITELIST - all_text_syms
    if absent:
        print("ERROR: whitelisted symbols not found in kernel binary:")
        for s in sorted(absent):
            print(f"  {s}")
        errors += 1

    assert errors == 0
    print(f"OK: {len(syms_with_endbr)} endbr64 symbol(s), all match the whitelist")


if __name__ == "__main__":
    main()
