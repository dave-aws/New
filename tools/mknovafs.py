#!/usr/bin/env python3
"""NOVA FS image builder (host side).

Layout (4 KiB blocks):
    block 0            superblock
    block 1..N         inode table (32 x 128-byte inodes per block)
    N+1                block allocation bitmap
    ..                 data blocks (direct + single indirect)

This mirrors kernel/fs/novafs.c; tests/host/test_novafs.py checks the two
implementations against each other.
"""
import os
import struct
import time

BLOCK = 4096
MAGIC = b"NOVAFS\0\0"
VERSION = 1
INODE_SIZE = 128
INODES_PER_BLOCK = BLOCK // INODE_SIZE
DIRENT_SIZE = 64
S_IFDIR = 0x4000
S_IFREG = 0x8000
MAX_DIRECT = 10

SUPER_FMT = "<8sIIQQQQQQQQQQ16s32s"
SUPER_SIZE = struct.calcsize(SUPER_FMT)
assert SUPER_SIZE <= BLOCK, SUPER_SIZE


class Inode:
    __slots__ = ("ino", "mode", "links", "size", "blocks", "indirect",
                 "dindirect", "mtime", "uid", "gid", "name", "children", "data")

    def __init__(self, ino, mode, name="", data=b""):
        self.ino = ino
        self.mode = mode
        self.links = 1
        self.size = len(data)
        self.blocks = []
        self.indirect = 0
        self.dindirect = 0
        self.mtime = int(time.time())
        self.uid = 0
        self.gid = 0
        self.name = name
        self.children = []
        self.data = data


def pack_inode(node):
    b = bytearray(INODE_SIZE)
    struct.pack_into("<HHIIQ", b, 0, node.mode, node.links, 0, 0, node.size)
    off = 16
    for i in range(MAX_DIRECT):
        val = node.blocks[i] if i < len(node.blocks) else 0
        struct.pack_into("<Q", b, off + i * 8, val)
    off += MAX_DIRECT * 8
    struct.pack_into("<Q", b, off, node.indirect)
    struct.pack_into("<Q", b, off + 8, node.dindirect)
    struct.pack_into("<Q", b, off + 16, node.mtime)
    struct.pack_into("<HH", b, off + 24, node.uid, node.gid)
    return bytes(b)


def pack_dirent(ino, typ, name):
    raw = name.encode("utf-8")[:51]
    rec = bytearray(DIRENT_SIZE)
    struct.pack_into("<II", rec, 0, ino, typ)
    rec[8] = len(raw)
    rec[9:9 + len(raw)] = raw
    return bytes(rec)


def build(source_dir, total_blocks=14336, label="NOVA"):
    inodes = []
    counter = [1]

    def new_inode(mode, name, data=b""):
        node = Inode(counter[0], mode, name, data)
        counter[0] += 1
        inodes.append(node)
        return node

    root = new_inode(S_IFDIR, "\\")

    def add_tree(node, host_path, depth=0):
        if depth > 8:
            return
        for entry in sorted(os.listdir(host_path)):
            full = os.path.join(host_path, entry)
            if os.path.isdir(full):
                child = new_inode(S_IFDIR, entry)
                node.children.append(child)
                add_tree(child, full, depth + 1)
            elif os.path.isfile(full):
                with open(full, "rb") as f:
                    data = f.read()
                child = new_inode(S_IFREG, entry, data)
                node.children.append(child)

    if source_dir and os.path.isdir(source_dir):
        add_tree(root, source_dir)

    for node in inodes:
        if node.mode == S_IFDIR:
            out = bytearray()
            out += pack_dirent(node.ino, 2, ".")
            out += pack_dirent(0, 0, "..")
            for ch in node.children:
                out += pack_dirent(ch.ino, 2 if ch.mode == S_IFDIR else 1, ch.name)
            node.data = bytes(out)
            node.size = len(node.data)

    inode_blocks = max(1, (len(inodes) + INODES_PER_BLOCK - 1) // INODES_PER_BLOCK)
    bitmap_block = 1 + inode_blocks
    data_start = bitmap_block + 1
    cursor = data_start

    image = bytearray(total_blocks * BLOCK)
    used = set()

    def alloc(payload):
        nonlocal cursor
        nblocks = max(1, (len(payload) + BLOCK - 1) // BLOCK)
        if cursor + nblocks > total_blocks:
            raise SystemExit("image is too small for the filesystem tree")
        first = cursor
        for i in range(nblocks):
            chunk = payload[i * BLOCK:(i + 1) * BLOCK]
            off = (first + i) * BLOCK
            image[off:off + len(chunk)] = chunk
            used.add(first + i)
        cursor += nblocks
        return first, nblocks

    for node in inodes:
        if not node.data:
            continue
        nblocks = (len(node.data) + BLOCK - 1) // BLOCK
        direct_count = min(nblocks, MAX_DIRECT)
        start, _ = alloc(node.data[:direct_count * BLOCK])
        node.blocks = [start + i for i in range(direct_count)]
        if nblocks > MAX_DIRECT:
            rest = node.data[direct_count * BLOCK:]
            indirect_entries = []
            iblock, inum = alloc(rest)
            indirect_entries.append(iblock)
            for i in range(inum):
                indirect_entries.append(iblock + i)
            if len(indirect_entries) > 512:
                raise SystemExit("file too large for single indirect blocks")
            payload = b"".join(struct.pack("<Q", blk) for blk in indirect_entries)
            istart, _ = alloc(payload)
            node.indirect = istart
            node.blocks += indirect_entries[1:]

    # inode table
    for i, node in enumerate(inodes):
        blk = 1 + i // INODES_PER_BLOCK
        off = (i % INODES_PER_BLOCK) * INODE_SIZE
        image[blk * BLOCK + off:blk * BLOCK + off + INODE_SIZE] = pack_inode(node)
        used.add(blk)

    bitmap = bytearray(BLOCK)
    bitmap[0] |= 1  # superblock
    for b in used:
        bitmap[b // 8] |= 1 << (b % 8)
    image[bitmap_block * BLOCK:bitmap_block * BLOCK + BLOCK] = bytes(bitmap)
    used.add(bitmap_block)

    free_blocks = total_blocks - len(used) - 1
    superblock = struct.pack(
        SUPER_FMT, MAGIC, VERSION, BLOCK, total_blocks, 1, len(inodes),
        1, inode_blocks, bitmap_block, 1, data_start,
        free_blocks, 0, bytes(range(16)), label.encode()[:31]
    )
    image[0:len(superblock)] = superblock
    return bytes(image)
