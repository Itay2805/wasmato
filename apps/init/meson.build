init_wasm = custom_target('init',
    input: wasm_compiler.process(files('main.c')),
    output: 'init',
    command: [wasm_ld, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default: true,
)
