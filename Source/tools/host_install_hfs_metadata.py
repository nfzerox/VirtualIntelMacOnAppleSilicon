"""Finalize BSD metadata on an unmounted, newly assembled HFS+ sparsebundle.

Native macOS tools create the filesystem and copy file data, forks, xattrs and
links. This prototype only restores catalog ownership/mode fields that the
unprivileged mount cannot set and records the boot folder/file. The layout is
defined in Apple's hfs_format.h. It is not a general filesystem editor.
"""

import os
from pathlib import Path
import plistlib
import stat
import struct


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


class SparseBundle:
    def __init__(self, path):
        self.path = Path(path)
        info = plistlib.loads((self.path / "Info.plist").read_bytes())
        if info["diskimage-bundle-type"] != "com.apple.diskimage.sparsebundle":
            raise ValueError("Expected a native sparsebundle")
        self.band_size = info["band-size"]
        self.size = info.get("size")

    def segments(self, offset, length):
        while length:
            band, start = divmod(offset, self.band_size)
            count = min(length, self.band_size - start)
            yield self.path / "bands" / f"{band:x}", start, count
            offset, length = offset + count, length - count

    def read(self, offset, length):
        chunks = []
        for path, start, count in self.segments(offset, length):
            with path.open("rb") as stream:
                stream.seek(start)
                chunk = stream.read(count)
            if len(chunk) != count:
                raise ValueError("Missing HFS+ metadata bytes in sparsebundle")
            chunks.append(chunk)
        return b"".join(chunks)

    def write(self, offset, data):
        for path, start, count in self.segments(offset, len(data)):
            with path.open("r+b") as stream:
                stream.seek(start)
                stream.write(data[:count])
            data = data[count:]


class RawDisk:
    """Read a detached, regular-file disk image without a sparsebundle map."""

    def __init__(self, path):
        self.path = Path(path)
        self.stream = os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb")
        info = os.fstat(self.stream.fileno())
        if not stat.S_ISREG(info.st_mode) or not info.st_size:
            self.stream.close()
            raise ValueError("Expected a detached regular-file disk image")
        self.size = info.st_size

    def close(self):
        self.stream.close()

    def __del__(self):
        if hasattr(self, "stream"):
            self.stream.close()

    def segments(self, offset, length):
        if not 0 <= offset <= offset + length <= self.size:
            raise ValueError("Read or write outside the raw guest disk")
        yield self.path, offset, length

    def read(self, offset, length):
        _, start, count = next(self.segments(offset, length))
        data = os.pread(self.stream.fileno(), count, start)
        if len(data) != length:
            raise ValueError("Short read from detached raw guest disk")
        return data

    def write(self, offset, data):
        _, start, count = next(self.segments(offset, len(data)))
        fd = os.open(self.path, os.O_RDWR | os.O_NOFOLLOW)
        try:
            if os.pwrite(fd, data, start) != count:
                raise OSError("Short write to detached raw guest disk")
        finally:
            os.close(fd)


def detached_image(path):
    return SparseBundle(path) if Path(path).is_dir() else RawDisk(path)


def restore_metadata(image, partition_offset, partition_size, permissions, *, boot=None):
    """Restore {CNID: (uid, gid, mode)} and optional (system folder, boot file) CNIDs.

    Call only after ejecting the new image. Get CNIDs and partition geometry
    from its native mount before ejecting. For hardlinks, use the shared inode
    from lstat(), not the catalog ID of a directory entry pointing to it.
    """
    image = SparseBundle(image)
    primary = partition_offset + 1024
    alternate = partition_offset + partition_size - 1024
    header = image.read(primary, 512)
    if header[:4] != b"H+\0\x04" or not u32(header, 4) & 0x100:
        raise ValueError("HFS+ target is not cleanly unmounted")
    block_size = u32(header, 40)
    if not block_size or block_size * u32(header, 44) != partition_size:
        raise ValueError("Unexpected HFS+ partition geometry")

    # HFSPlusVolumeHeader.catalogFile and its inline allocation extents.
    catalog = 272
    length, _, blocks = struct.unpack_from(">QII", header, catalog)
    extents = [extent for extent in struct.iter_unpack(">II", header[288:352]) if extent[1]]
    if sum(count for _, count in extents) != blocks:
        raise ValueError("The new HFS+ catalog needs extent overflow support")

    def catalog_segments(offset, count):
        if offset + count > length:
            raise ValueError("HFS+ catalog access exceeds the fork")
        for start, blocks in extents:
            size = blocks * block_size
            if offset < size:
                span = min(size - offset, count)
                yield partition_offset + start * block_size + offset, span
                count -= span
                if not count:
                    return
                offset = 0
            else:
                offset -= size
        raise ValueError("Incomplete HFS+ catalog extents")

    def catalog_read(offset, count):
        return b"".join(image.read(start, span) for start, span in catalog_segments(offset, count))

    tree = catalog_read(0, 64)
    node_size = u16(tree, 32)
    if tree[8] != 1 or node_size < 4096 or node_size & (node_size - 1):
        raise ValueError("Invalid HFS+ catalog header node")
    node = u32(tree, 24)
    visited, found, changes = set(), {}, []
    wanted = set(permissions) | set(boot or ())
    while node and len(found) != len(wanted):
        if node in visited:
            raise ValueError("HFS+ catalog leaf cycle")
        visited.add(node)
        base = node * node_size
        leaf = bytearray(catalog_read(base, node_size))
        if leaf[8] != 255:
            raise ValueError("Invalid HFS+ catalog leaf")
        count = u16(leaf, 10)
        dirty = False
        for entry in range(count):
            offset = u16(leaf, node_size - 2 * (entry + 1))
            end = u16(leaf, node_size - 2 * (entry + 2))
            data = offset + 2 + u16(leaf, offset)
            if not 14 <= offset < data <= end - 2 <= node_size - 2 * (count + 1):
                raise ValueError("Invalid HFS+ catalog record boundaries")
            kind = u16(leaf, data)
            if kind not in (1, 2):
                continue  # thread records do not carry file metadata
            cnid = u32(leaf, data + 8)
            if cnid not in wanted:
                continue
            found[cnid] = kind
            if cnid in permissions:
                uid, gid, mode = permissions[cnid]
                if end - data < 48:
                    raise ValueError("Truncated HFS+ catalog file or folder")
                current_mode = u16(leaf, data + 42)
                if stat.S_IFMT(current_mode) != stat.S_IFMT(mode):
                    raise ValueError(f"HFS+ catalog item {cnid} differs in type from the manifest")
                # Preserve the file flags and special field (e.g. hardlink count).
                if (u32(leaf, data + 32), u32(leaf, data + 36), current_mode) != (uid, gid, mode):
                    struct.pack_into(">II", leaf, data + 32, uid, gid)
                    struct.pack_into(">H", leaf, data + 42, mode)
                    dirty = True
        if dirty:
            changes.append((base, leaf))
        node = u32(leaf, 0)
    if found.keys() != wanted:
        raise ValueError(f"HFS+ catalog is missing {len(wanted - found.keys())} requested items")

    if boot is not None:
        folder, executable = boot
        if found[folder] != 1 or found[executable] != 2 or image.read(alternate, 4) != header[:4]:
            raise ValueError("Invalid HFS+ boot folder, file, or alternate volume header")
    for offset, value in changes:
        for start, span in catalog_segments(offset, len(value)):
            image.write(start, value[:span])
            value = value[span:]
    if boot is not None:
        for offset in (primary, alternate):
            image.write(offset + 80, struct.pack(">II", folder, executable))
            image.write(offset + 100, struct.pack(">I", folder))
