clang   = find_program('clang')
wasm_ld = find_program('wasm-ld')

wasm_c_args = (
    ['--target=wasm32',
     '-Wall', '-Werror', '-std=gnu23',
     '-flto', '-g',
     '-fvisibility=hidden',
     '-nostdlib', '-nostdinc',
     '-isystem', clang_resource_dir / 'include']
    + (is_optimize ? ['-O3'] : [])
    + (is_debug    ? ['-D__DEBUG__', '-Wno-unused-function',
                      '-Wno-unused-label', '-Wno-unused-variable'] : [])
)

wasm_compiler = generator(clang,
    output: '@BASENAME@.wasm.o',
    depfile: '@BASENAME@.wasm.d',
    arguments: wasm_c_args + ['-MMD', '-MF', '@DEPFILE@', '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

subdir('init')
