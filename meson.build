project('tomatos', 'c',
    version: '0.1.0',
    meson_version: '>= 1.3.0',
    default_options: [
        'c_std=gnu23',
        'warning_level=0',
        'b_lto=false',
        'b_pie=false',
        'b_staticpic=false',
        'default_library=static',
    ],
)

clang_resource_dir = run_command(
    'clang', '--print-resource-dir',
    capture: true, check: true,
).stdout().strip()

is_debug    = get_option('debug')
is_optimize = get_option('optimization') != '0'

freestanding_c_args = (
    ['-Wall', '-Werror',
     '-march=x86-64-v3',
     '-flto', '-g',
     '-fno-omit-frame-pointer',
     '-fvisibility=hidden',
     '-Wno-unused-label',
     '-fno-jump-tables',
     '-fno-pie', '-fno-pic',
     '-ffreestanding', '-fno-builtin',
     '-nostdlib', '-nostdinc',
     '-mno-red-zone']
    + (is_optimize ? ['-O3'] : [])
    + (is_debug    ? ['-fsanitize=undefined', '-D__DEBUG__',
                      '-Wno-unused-function', '-Wno-unused-variable'] : [])
)

freestanding_link_args = (
    ['-fuse-ld=lld', '-flto', '-nostdlib', '-static']
    + (is_optimize ? ['-O3'] : [])
)

inc_clang_sys    = include_directories(clang_resource_dir / 'include')
inc_common       = include_directories('src/common')
inc_kernel       = include_directories('src/kernel')
inc_runtime      = include_directories('src/runtime')
inc_limine_proto = include_directories('lib/limine-protocol/include')
inc_spidir       = include_directories('lib/spidir/c-api/include')

freestanding_dep = declare_dependency(
    compile_args:      freestanding_c_args,
    include_directories: inc_clang_sys,
)

subdir('src/common')
subdir('src/rust-libs')
subdir('src/runtime')
subdir('src/kernel')
subdir('apps')

# --------------------------------------------------------------------------
# Limine installer (native host binary — no cross flags)
# --------------------------------------------------------------------------
limine_exe = executable('limine',
    'lib/limine/limine.c',
    native: true,
    override_options: ['warning_level=0'],
    link_args: ['-fuse-ld=lld'],
)

# --------------------------------------------------------------------------
# Disk image
# --------------------------------------------------------------------------
image = custom_target('tomato.img',
    output: 'tomato.img',
    input: [kernel_exe, init_wasm],
    depends: limine_exe,
    command: [
        find_program('sh'), '-euc',
        '''IMG="$1"; KERNEL="$2"; INITRD="$3"; LIMINE="$4"; SRCDIR="$5"
rm -f "$IMG"
dd if=/dev/zero bs=1M count=0 seek=64 of="$IMG"
sgdisk "$IMG" -n 1:2048 -t 1:ef00 -m 1
"$LIMINE" bios-install "$IMG"
mformat -i "$IMG"@@1M
mmd -i "$IMG"@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$IMG"@@1M "$KERNEL" ::/boot/kernel
mcopy -i "$IMG"@@1M "$SRCDIR/artifacts/limine.conf" ::/boot/limine
mcopy -i "$IMG"@@1M "$INITRD" ::/boot/initrd
mcopy -i "$IMG"@@1M "$SRCDIR/lib/limine/limine-bios.sys" ::/boot/limine
mcopy -i "$IMG"@@1M "$SRCDIR/lib/limine/BOOTX64.EFI" ::/EFI/BOOT
mcopy -i "$IMG"@@1M "$SRCDIR/lib/limine/BOOTIA32.EFI" ::/EFI/BOOT''',
        '--',
        '@OUTPUT@', '@INPUT0@', '@INPUT1@',
        limine_exe.full_path(),
        '@SOURCE_ROOT@',
    ],
    build_always_stale: true,
    console: true,
)

# --------------------------------------------------------------------------
# run / check-ibt targets
# --------------------------------------------------------------------------
run_target('run',
    command: [
        find_program('qemu-system-x86_64'),
        '--enable-kvm',
        '-cpu', 'host,migratable=off',
        '-machine', 'q35',
        '-m', '256M',
        '-smp', '4',
        '-s',
        '-drive', 'format=raw,file=' + image.full_path() + ',media=disk',
        '-debugcon', 'stdio',
        '-monitor', 'telnet:127.0.0.1:5555,server,nowait',
        '-no-reboot',
        '-no-shutdown',
    ],
    depends: image,
)

run_target('check-ibt',
    command: [
        find_program('python3'),
        meson.project_source_root() / 'scripts/check-ibt-whitelist.py',
        kernel_exe.full_path(),
    ] + (is_debug ? ['--debug'] : []),
    depends: kernel_exe,
)
