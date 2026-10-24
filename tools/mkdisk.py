#!/usr/bin/env python3
"""Assemble the bootable NOVA OS disk image.

   LBA 0          stage 1 (MBR)
   LBA 1..60      stage 2
   LBA 64..       flat kernel image
   LBA 16384..    NOVA FS partition (MBR type 0x7F)

The partition is real: the kernel finds it by reading the MBR, exactly like
any other disk driver would.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mknovafs

SECTOR = 512
KERNEL_LBA = 64
PART_LBA = 16384
CYL = 16 * 63

def chs(lba):
    cyl = lba // CYL
    head = (lba % CYL) // 63
    sec = (lba % 63) + 1
    if cyl > 1023:
        cyl = 1023
        head = 254
        sec = 63
    return bytes([head, sec | ((cyl >> 2) & 0xC0), cyl & 0xFF])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage1", required=True)
    ap.add_argument("--stage2", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--userland", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size-mb", type=int, default=64)
    args = ap.parse_args()

    total_sectors = args.size_mb * 1024 * 1024 // SECTOR
    part_sectors = total_sectors - PART_LBA
    image = bytearray(total_sectors * SECTOR)

    with open(args.stage1, "rb") as f:
        stage1 = f.read()
    with open(args.stage2, "rb") as f:
        stage2 = f.read()
    with open(args.kernel, "rb") as f:
        kernel = f.read()

    if len(stage1) != 512:
        sys.exit("stage1 must be exactly 512 bytes")
    if len(stage2) > 60 * SECTOR:
        sys.exit("stage2 does not fit in its 60-sector slot")
    if len(kernel) > (PART_LBA - KERNEL_LBA) * SECTOR:
        sys.exit("kernel overlaps the partition")

    image[0:512] = stage1
    image[SECTOR:SECTOR + len(stage2)] = stage2
    image[KERNEL_LBA * SECTOR:KERNEL_LBA * SECTOR + len(kernel)] = kernel

    fs_blocks = part_sectors * SECTOR // mknovafs.BLOCK
    fs = mknovafs.build(args.userland, total_blocks=fs_blocks, label="NOVA")
    if len(fs) != fs_blocks * mknovafs.BLOCK:
        sys.exit("filesystem image size mismatch")
    off = PART_LBA * SECTOR
    image[off:off + len(fs)] = fs

    entry = struct.pack(
        "<B3sB3sII",
        0x80,               # bootable
        chs(PART_LBA),
        0x7F,               # NOVA FS partition type
        chs(PART_LBA + part_sectors - 1),
        PART_LBA,
        part_sectors,
    )
    assert len(entry) == 16
    image[0x1BE:0x1BE + 16] = entry
    image[0x1FE:0x200] = b"\x55\xAA"

    with open(args.out, "wb") as f:
        f.write(image)
    print(f"disk: {args.out}")
    print(f"  kernel  {len(kernel)} bytes at LBA {KERNEL_LBA}")
    print(f"  NOVA FS {len(fs) // 1024} KiB at LBA {PART_LBA} ({fs_blocks} blocks)")


if __name__ == "__main__":
    main()
