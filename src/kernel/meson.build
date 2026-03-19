kernel_sources = files(
    'acpi/acpi.c',
    'arch/apic.c',
    'arch/gdt.c',
    'arch/intr.c',
    'arch/intr.S',
    'arch/smp.c',
    'debug/log.c',
    'lib/ipi.c',
    'lib/pcpu.c',
    'limine_requests.c',
    'main.c',
    'mem/alloc.c',
    'mem/early.c',
    'mem/mappings.c',
    'mem/phys.c',
    'mem/phys_map.c',
    'mem/virt.c',
    'mem/vmar.c',
    'time/tsc.c',
    'user/stack.c',
    'user/syscall.c',
    'user/syscall.S',
    'user/user.S',
) + common_all_sources

kernel_extra_args = [
    '-mgeneral-regs-only',
    '-fcf-protection=branch',
    '-mcmodel=kernel',
    '-mshstk',
    '-DSTB_SPRINTF_NOFLOAT',
]

linker_script = files('linker.ld')

runtime_c_obj = custom_target('runtime_c_obj',
    input: files('runtime.c'),
    output: 'runtime.c.o',
    depends: runtime_exe,
    command: [
        'clang', '-target', 'x86_64-pc-none-elf',
        '-std=gnu23',  # not injected by Meson for custom_target raw commands
        '--embed-dir=' + meson.project_build_root() / 'src/runtime',
    ] + freestanding_c_args + kernel_extra_args + [
        '-isystem', clang_resource_dir / 'include',
        '-I' + meson.project_source_root() / 'src/kernel',
        '-I' + meson.project_source_root() / 'src/common',
        '-I' + meson.project_source_root() / 'lib/limine-protocol/include',
        '-c', '@INPUT@', '-o', '@OUTPUT@',
    ],
)

kernel_exe = executable('kernel',
    kernel_sources,
    objects: runtime_c_obj.to_list(),
    dependencies: freestanding_dep,
    c_args: kernel_extra_args,
    include_directories: [inc_common, inc_kernel, inc_limine_proto],
    link_args: freestanding_link_args + ['-T' + (meson.current_source_dir() / 'linker.ld')],
    link_depends: [linker_script, runtime_exe],
)
