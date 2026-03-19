cargo = find_program('cargo')

rustup_toolchain  = 'nightly-2025-10-09'
cargo_target_name = 'x86_64-unknown-linux-none'
cargo_target_dir  = meson.project_build_root() / 'cargo-target'
cargo_profile     = is_optimize ? 'release' : 'debug'
cargo_profile_arg = is_optimize ? ['--release'] : []

rust_lib_out = cargo_target_dir / cargo_target_name / cargo_profile / 'librust_libs.a'

rustflags = ' '.join([
    '-C force-frame-pointers=yes',
    '-Z no-jump-tables',
    '-C linker-plugin-lto=yes',
    '-C relocation-model=static',
    '-C target-cpu=x86-64-v3',
    '-Z cf-protection=full',
    '-C no-redzone',
])

rust_libs = custom_target('rust_libs',
    output: 'librust_libs.a',
    env: {'RUSTFLAGS': rustflags},
    depend_files: files(
        'Cargo.toml',
        'Cargo.lock',
        'src/lib.rs',
        'src/log.rs',
        'src/platform.rs',
    ),
    command: [
        find_program('sh'), '-euc',
        '"$1" +"$2" rustc --manifest-path "$3" --target "$4" -Zbuild-std=core,alloc --target-dir "$5" ${7+$7} && (cmp -s "$6" @OUTPUT@ || cp "$6" @OUTPUT@)',
        '--',
        cargo,
        rustup_toolchain,
        meson.project_source_root() / 'src/rust-libs/Cargo.toml',
        cargo_target_name,
        cargo_target_dir,
        rust_lib_out,
    ] + cargo_profile_arg,
    console: true,
)
