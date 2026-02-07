OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    limine_requests PT_LOAD FLAGS(4);
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0xffffffff80000000;

    .limine_requests : ALIGN(4096) {
        __kernel_limine_requests_base = .;
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(4096);
    __kernel_limine_requests_page_count = ABSOLUTE(. - __kernel_limine_requests_base) / 4096;

    .text : ALIGN(4096) {
        __kernel_text_base = .;
        *(.text .text.*)
    } :text

    . = ALIGN(4096);
    __kernel_text_page_count = ABSOLUTE(. - __kernel_text_base) / 4096;

    .rodata : ALIGN(4096) {
        __kernel_rodata_base = .;
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4096);
    __kernel_rodata_page_count = ABSOLUTE(. - __kernel_rodata_base) / 4096;

    .data : ALIGN(4096) {
        __kernel_data_base = .;
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(4096);
    __kernel_data_page_count = ABSOLUTE(. - __kernel_data_base) / 4096;

}
