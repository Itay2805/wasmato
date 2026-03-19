# Wasmato

A bare-metal x86-64 operating system kernel that runs WebAssembly programs natively via a JIT compiler.

## Building

```sh
meson setup build --cross-file cross/x86_64-none-elf.ini
ninja -C build
```

By default this is a debug build. For a release build:
```sh
meson setup build --cross-file cross/x86_64-none-elf.ini --buildtype=release
ninja -C build
```

Create a bootable disk image
```sh
ninja -C build tomato.img
```

Run in QEMU
```sh
ninja -C build run
```

Check IBT whitelist (for the kernel)
```sh
ninja -C build check-ibt
```
