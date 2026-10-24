#!/bin/sh
# Stage the NOVA filesystem tree that ends up in the disk image and build the
# bundled Win32 applications with the mingw-w64 cross compiler fetched by
# tools/fetch-toolchain.py.
set -e
cd "$(dirname "$0")/.."
BUILD=build
ROOT=$BUILD/userland
MINGW=$PWD/.toolchain/bin/x86_64-w64-mingw32-gcc
DLLTOOL=$PWD/.toolchain/bin/x86_64-w64-mingw32-dlltool

rm -rf "$ROOT"
mkdir -p "$ROOT/Apps" "$ROOT/Windows/System32" "$ROOT/Users/Nova" "$ROOT/Temp"

# ---------------------------------------------------------------- import libs
# Import libraries describe the Win32 surface NOVA provides.  The PE loader
# matches imports by DLL name and function name, so these definitions are the
# single source of truth for both the toolchain and the subsystem.
mkdir -p "$BUILD/implib"
for dll in kernel32 user32 gdi32 shell32 msvcrt advapi32; do
    def="userland/defs/$dll.def"
    if [ -f "$def" ]; then
        "$DLLTOOL" -k -d "$def" -l "$BUILD/implib/lib$dll.a" 2>/dev/null || true
    fi
done

# ------------------------------------------------------------------- programs
if [ -x "$MINGW" ]; then
    CFLAGS="-O2 -nostdlib -ffreestanding -fno-builtin -mno-stack-arg-probe \
            -Wall -Wextra -Wno-unused-parameter -Iuserland/include"
    LDFLAGS="-nostdlib -Wl,--subsystem,windows -Wl,--entry,WinMainCRTStartup \
             -Wl,--image-base,0x140000000 -Wl,--disable-reloc-section"
    for src in userland/apps/*.c; do
        [ -f "$src" ] || continue
        name=$(basename "$src" .c)
        echo "  PE  $name.exe"
        "$MINGW" $CFLAGS -c "$src" -o "$BUILD/$name.o"
        "$MINGW" $LDFLAGS "$BUILD/$name.o" \
            -L"$BUILD/implib" -lkernel32 -luser32 -lgdi32 -lshell32 -lmsvcrt \
            -o "$ROOT/Apps/$name.exe"
    done
else
    echo "  (mingw-w64 not present: skipping bundled .exe programs)"
fi

# --------------------------------------------------------------- system files
cat > "$ROOT/Windows/System32/nova.ini" <<'EOF'
[NOVA]
Version=0.1
Codename=Genesis
Subsystem=Win32
EOF

cat > "$ROOT/Users/Nova/welcome.txt" <<'EOF'
Welcome to NOVA OS.

This is a native operating system: its own bootloader, kernel, scheduler,
virtual memory manager, filesystem and window manager.  The Win32 subsystem
runs PE32+ executables directly - try the Terminal and type:

    run C:\APPS\HELLO.EXE
    run C:\APPS\NOTEPAD.EXE
EOF

cp README.md "$ROOT/Users/Nova/README.md" 2>/dev/null || true

echo "  userland staged in $ROOT"
