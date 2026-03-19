runtime_core_sources = files(
    'core/entry.c',
    'core/sched.c',
    'core/thread.c',
    'core/thread.S',
    'core/timer.c',
    'core/wait.c',
) + common_core_sources

runtime_sources = files(
    'alloc/aligned_alloc.c',
    'alloc/malloc.c',
    'alloc/rust.c',
    'debug/log.c',
    'lib/stb_ds.c',
    'lib/string.c',
    'main.c',
    'proc/thread.c',
    'sync/mutex.c',
    'wasm/buffer.c',
    'wasm/jit/function.c',
    'wasm/jit/jit.c',
    'wasm/module.c',
    'wasm/wasm.c',
) + common_extra_sources

runtime_extra_args = [
    '-mxsave', '-mxsaveopt', '-mwaitpkg',
    '-mrdpid', '-mfsgsbase',
    '-fcf-protection=full',
]

runtime_core_lib = static_library('runtime-core',
    runtime_core_sources,
    dependencies: freestanding_dep,
    c_args: runtime_extra_args + ['-mgeneral-regs-only'],
    include_directories: [inc_common, inc_runtime],
)

runtime_exe = executable('runtime',
    runtime_sources,
    dependencies: freestanding_dep,
    c_args: runtime_extra_args + ['-mcmodel=small'],
    include_directories: [inc_common, inc_runtime, inc_spidir],
    link_args: freestanding_link_args + ['-mcmodel=small'],
    link_with: [runtime_core_lib, rust_libs],
)
