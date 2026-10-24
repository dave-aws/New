#!/usr/bin/env python3
"""Fetch a self-contained developer toolchain without root privileges.

We only have a stock Debian userland in the build sandbox: no emulator and no
PE (Windows) cross compiler.  This script resolves the Debian package
dependencies for the packages we need (QEMU for boot testing, mingw-w64 for
producing real PE32+ test programs) and extracts them into .toolchain/root.

Nothing is installed system-wide; wrapper scripts are generated in
.toolchain/bin so the build can use them without touching $PATH globally.
"""
import lzma
import os
import re
import subprocess
import urllib.request

MIRROR = "https://deb.debian.org/debian"
SUITE = "bookworm"
ARCH = "amd64"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".toolchain"))
PKGDIR = os.path.join(ROOT, "debs")
EXTRACT = os.path.join(ROOT, "root")
BINDIR = os.path.join(ROOT, "bin")

ROOTS = [
    "qemu-system-x86",
    "nasm",
    "mingw-w64-common",
    "mingw-w64-x86-64-dev",
    "binutils-mingw-w64-x86-64",
    "gcc-mingw-w64-x86-64",
    "gcc-mingw-w64-x86-64-posix",
    "g++-mingw-w64-x86-64-posix",
]


def fetch(url, dest=None):
    if dest and os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    fd = urllib.request.urlopen(url, timeout=120)
    data = fd.read()
    if dest:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
    return data


def load_index():
    cache = os.path.join(ROOT, "Packages.xz")
    if not os.path.exists(cache):
        print("[toolchain] downloading package index ...")
        fetch(f"{MIRROR}/dists/{SUITE}/main/binary-{ARCH}/Packages.xz", cache)
    raw = lzma.open(cache, "rt", encoding="utf-8", errors="replace").read()
    pkgs = {}
    for stanza in raw.split("\n\n"):
        fields = {}
        key = None
        for line in stanza.splitlines():
            if line.startswith(" ") and key:
                fields[key] += " " + line.strip()
            elif ": " in line:
                key, val = line.split(": ", 1)
                fields[key] = val.strip()
        if "Package" in fields:
            fields["_deps"] = parse_deps(fields.get("Depends", "") + "," + fields.get("Pre-Depends", ""))
            pkgs.setdefault(fields["Package"], []).append(fields)
    for name, lst in pkgs.items():
        lst.sort(key=lambda p: p.get("Version", ""), reverse=True)
    return pkgs


def parse_deps(text):
    out = []
    for group in text.split(","):
        group = group.strip()
        if not group:
            continue
        alts = []
        for alt in group.split("|"):
            m = re.match(r"([a-z0-9][a-z0-9+.\-]*)", alt.strip())
            if m:
                alts.append(m.group(1))
        if alts:
            out.append(alts)
    return out


INSTALLED_DEPS_OK = {
    # present in the base image; pulling them in would be pointless churn
    "libc6", "libgcc-s1", "libstdc++6", "gcc-12-base", "zlib1g", "libzstd1",
    "liblzma5", "libbz2-1.0", "libselinux1", "libpcre2-8-0", "libtinfo6",
    "libcap2", "libcom-err2", "libkrb5-3", "libk5crypto3", "libkrb5support0",
    "libkeyutils1", "libgssapi-krb5-2", "libssl3", "libcrypt1", "libnsl2",
    "libtirpc3", "libidn2-0", "libunistring2", "libffi8", "libgmp10",
    "libgnutls30", "libhogweed6", "libnettle8", "libp11-kit0", "libtasn1-6",
    "libbrotli1", "libpsl5", "libnghttp2-14", "librtmp1", "libssh2-1",
    "libldap-2.5-0", "libsasl2-2", "libsasl2-modules-db", "libgpg-error0",
    "libgcrypt20", "liblz4-1", "libsystemd0", "libudev1", "libacl1",
    "libattr1", "libaudit1", "libpam0g", "libdb5.3", "libblkid1",
    "libmount1", "libsmartcols1", "libuuid1", "libfdisk1", "libss2",
    "libext2fs2", "libsqlite3-0", "libicu72", "libxml2", "libjpeg62-turbo",
    "libpng16-16", "libfreetype6", "libfontconfig1", "libexpat1",
}


def resolve(pkgs, roots):
    chosen = {}
    queue = []

    def pick(name):
        if name in pkgs:
            return pkgs[name][0]
        providers = [p for lst in pkgs.values() for p in lst
                     if name in p.get("Provides", "").replace(" ", "").split(",")]
        return providers[0] if providers else None

    for r in roots:
        p = pick(r)
        if p:
            queue.append(p)
        else:
            print(f"[toolchain] WARNING: no package for {r}")

    while queue:
        p = queue.pop()
        name = p["Package"]
        if name in chosen or name in INSTALLED_DEPS_OK:
            continue
        chosen[name] = p
        for alts in p["_deps"]:
            dep = None
            for alt in alts:
                dep = pick(alt)
                if dep:
                    break
            if dep:
                queue.append(dep)
    return chosen


def main():
    os.makedirs(PKGDIR, exist_ok=True)
    os.makedirs(EXTRACT, exist_ok=True)
    os.makedirs(BINDIR, exist_ok=True)
    pkgs = load_index()
    chosen = resolve(pkgs, ROOTS)
    print(f"[toolchain] {len(chosen)} packages to unpack")
    for name, p in sorted(chosen.items()):
        url = f"{MIRROR}/{p['Filename']}"
        dest = os.path.join(PKGDIR, os.path.basename(p["Filename"]))
        try:
            fetch(url, dest)
        except Exception as e:  # noqa: BLE001
            print(f"[toolchain] failed {name}: {e}")
            continue
        subprocess.run(["dpkg-deb", "-x", dest, EXTRACT], check=False)
        print(f"[toolchain] unpacked {name} {p.get('Version','')}")

    # Wrapper scripts -------------------------------------------------------
    env = ("#!/bin/sh\n"
           f"LD_LIBRARY_PATH={EXTRACT}/usr/lib/x86_64-linux-gnu:{EXTRACT}/lib/x86_64-linux-gnu"
           ":$LD_LIBRARY_PATH\n"
           "export LD_LIBRARY_PATH\n"
           f"exec {EXTRACT}/usr/bin/PROG \"$@\"\n")
    for prog in ("qemu-system-x86_64", "qemu-img"):
        src = os.path.join(EXTRACT, "usr/bin", prog)
        if os.path.exists(src):
            path = os.path.join(BINDIR, prog)
            with open(path, "w") as f:
                f.write(env.replace("PROG", prog))
            os.chmod(path, 0o755)
    for prog in os.listdir(os.path.join(EXTRACT, "usr/bin")):
        if not prog.startswith(("x86_64-w64-mingw32-", "i686-w64-mingw32-")):
            continue
        src = os.path.join(EXTRACT, "usr/bin", prog)
        if os.path.islink(src) or not os.path.isfile(src):
            continue
        path = os.path.join(BINDIR, prog)
        with open(path, "w") as f:
            f.write(env.replace("PROG", prog))
        os.chmod(path, 0o755)
    print(f"[toolchain] wrappers in {BINDIR}")


if __name__ == "__main__":
    main()
