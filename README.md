# NOVA OS

NOVA OS is a from-scratch, lightweight x86-64 desktop operating system with a
familiar Windows-style shell and a native Win32/PE compatibility subsystem.
It does not contain Linux, BSD or any other existing OS kernel code: the
bootloader, kernel, memory manager, scheduler, filesystem, window manager and
PE loader in this repository are original.

> **Status: 0.1 "Genesis"** — boots on BIOS/legacy x86-64 hardware (and QEMU),
> runs a preemptive multi-threaded kernel with its own filesystem and desktop,
> and executes PE32+ Windows executables through the Win32 subsystem.

## Quick start

```sh
./tools/fetch-toolchain.py    # optional: QEMU + mingw-w64 into .toolchain/
make                          # builds build/nova.img
make run                      # boot with a graphical window
make run-headless             # boot headless, kernel log on stdio
make test                     # host unit tests + QEMU boot smoke test
```

## What is inside

| Layer | Description |
|-------|-------------|
| Boot | Custom 2-stage BIOS loader: MBR -> real-mode stage 2 -> E820, VBE, A20, long mode |
| Kernel | Monolithic x86-64 kernel, higher half, 4-level paging, preemptive scheduler |
| Memory | E820 bitmap frame allocator, direct physical map, demand-grown heap |
| Devices | 8259 PIC, 8254 PIT, PS/2 keyboard and mouse, ATA PIO, VBE framebuffer, serial |
| Filesystem | NOVA FS (own on-disk format) mounted from an MBR partition |
| Desktop | Compositor, windows, taskbar, start menu, terminal, file manager, settings |
| Win32 | PE32/PE32+ loader, relocations, imports/exports, DLLs, kernel32/user32/gdi32 |

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design and
[docs/WIN32.md](docs/WIN32.md) for compatibility details.
