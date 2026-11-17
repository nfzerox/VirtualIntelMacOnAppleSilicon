"""Read detached APFS trees used by the Catalina and Big Sur install workers.

The block and record layouts are from the Apple File System Reference.
This is deliberately not a filesystem implementation: 4 KiB, single-store
containers and unencrypted metadata are the only inputs. Big Sur's Intel
sealed fsroot can also be traversed against an external SHA-256 root hash.
"""

from bisect import bisect_right
from dataclasses import dataclass
from functools import cached_property
from hashlib import sha256
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
from uuid import UUID

from host_install_apfs import apfs_extent, attached_images
from host_install_hfs_metadata import SparseBundle, detached_image


BLOCK = 4096
MOD = 0xffffffff
OID_MASK = (1 << 60) - 1
INODE, XATTR, DREC = 3, 4, 9
FIRMLINK = b"com.apple.fs.firmlink\0"
# Use the preceding equal-length name while the scratch System volume is
# mounted. A detached finalization can replace the key without rebalancing.
PLACEHOLDER = b"com.apple.fs.firmlinj\0"
SF_FIRMLINK = 0x00800000


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


def block_checksum(data):
    a = b = 0
    for word, in struct.iter_unpack("<I", memoryview(data)[8:]):
        a += word
        b += a
    first = MOD - (a + b) % MOD
    second = MOD - (a + first) % MOD
    return first | (second << 32)


def fixed_table_size(key_size, value_size):
    """APFS reserves slots for a full non-root fixed-record node."""
    return ((BLOCK - 56) // (4 + key_size + value_size)) * 4


class RegularImage:
    """Read-only GPT source; never pass through device nodes."""

    def __init__(self, stream):
        self.fd = stream.fileno()
        if not stat.S_ISREG(os.fstat(self.fd).st_mode):
            raise ValueError("APFS analysis requires a regular image file")

    def read(self, offset, size):
        data = os.pread(self.fd, size, offset)
        if len(data) != size:
            raise ValueError("Short read from the APFS image")
        return data


class MappedQCOWImage(RegularImage):
    """Inspect a standalone reference disk without mounting or copying it.

    QEMU resolves the QCOW2 allocation tables. Read only mapped, uncompressed
    data directly from the original regular file; never open it for writes.
    """

    @classmethod
    def from_qemu(cls, stream, qemu_img):
        extents = json.loads(subprocess.check_output(
            (qemu_img, "map", "--output=json", "-f", "qcow2", stream.name), stdin=subprocess.DEVNULL))
        result = cls(stream, extents)
        result.path = Path(stream.name)
        return result

    def __init__(self, stream, extents):
        super().__init__(stream)
        self.extents = []
        end = 0
        file_size = os.fstat(self.fd).st_size
        for extent in extents:
            start, length = extent["start"], extent["length"]
            if start != end or length <= 0 or extent["depth"] != 0:
                raise ValueError("Invalid or backed QCOW2 reference extent")
            offset = None if extent["zero"] else extent.get("offset")
            if not extent["zero"] and (extent.get("compressed") or offset is None or
                                       not 0 <= offset <= file_size - length):
                raise ValueError("QCOW2 reference extent does not have a readable file offset")
            end += length
            self.extents.append((start, end, offset))
        self.starts = [item[0] for item in self.extents]
        self.size = end

    def ranges(self, offset, size):
        if not 0 <= offset <= offset + size <= self.size:
            raise ValueError("Read outside the QCOW2 disk")
        while size:
            start, end, physical = self.extents[bisect_right(self.starts, offset) - 1]
            count = min(size, end - offset)
            yield offset, count, None if physical is None else physical + offset - start
            offset += count
            size -= count

    def allocated_ranges(self, offset, size):
        for start, count, physical in self.ranges(offset, size):
            if physical is not None:
                yield start, count

    def read(self, offset, size):
        data = bytearray()
        for _, count, physical in self.ranges(offset, size):
            data += bytes(count) if physical is None else super().read(physical, count)
        return bytes(data)


@dataclass
class Entry:
    key_offset: int
    key: bytes
    value_offset: int
    value: bytes


@dataclass
class Node:
    address: int
    data: bytes
    leaf: bool
    entries: list[Entry]


def node_records(data, *, fixed=False):
    flags, level, count, table_offset, table_length = struct.unpack_from("<HHIHH", data, 32)
    leaf = bool(flags & 2)
    slot_size = 4 if fixed else 8
    keys, values = 56 + table_offset + table_length, BLOCK - (40 if flags & 1 else 0)
    if bool(flags & 4) != fixed or leaf != (level == 0) or keys > values or count * slot_size > table_length:
        raise ValueError("Unexpected APFS B-tree node layout")
    entries = []
    for index in range(count):
        slot = 56 + table_offset + index * slot_size
        if fixed:
            ko, vo = struct.unpack_from("<HH", data, slot)
            kl, vl = 16, (16 if leaf else 8)
        else:
            ko, kl, vo, vl = struct.unpack_from("<HHHH", data, slot)
        kp, vp = keys + ko, values - vo
        if not keys <= kp < kp + kl <= values or not keys <= vp < vp + vl <= values:
            raise ValueError("Invalid APFS B-tree record span")
        entries.append(Entry(kp, data[kp:kp + kl], vp, data[vp:vp + vl]))
    return flags, level, leaf, entries


def hashed_tree(read, resolve, root, root_hash):
    """Read the headerless SHA-256 fsroot tree produced by Big Sur Intel."""
    pending, seen = [(root, root_hash, None)], set()
    while pending:
        oid, digest, expected_level = pending.pop()
        address = resolve(oid)
        if address in seen:
            raise ValueError("Authenticated APFS tree contains a repeated block")
        seen.add(address)
        data = read(address)
        if len(data) != BLOCK or sha256(data).digest() != digest:
            raise ValueError(f"APFS node hash mismatch at block {address:#x}")
        flags, level, leaf, entries = node_records(data)
        # The hash covers the whole block, including the zeroed object header.
        if any(data[:32]) or not flags & 0x10 or (not leaf and not flags & 8) or \
                bool(flags & 1) != (oid == root) or (expected_level is not None and level != expected_level):
            raise ValueError("Unexpected APFS authenticated node layout")
        if not leaf:
            for entry in reversed(entries):
                if len(entry.value) not in (40, 72):
                    raise ValueError("Unexpected APFS SHA-256 child record")
                # Big Sur stores these child IDs relative to the tree root,
                # including at the levels beneath the first index node.
                pending.append((root + u64(entry.value, 0), entry.value[8:40], level - 1))
        yield Node(address, data, leaf, entries)


class APFS:
    def __init__(self, image):
        self.image = image
        self.offset, size = apfs_extent(image)
        self.blocks = size // BLOCK
        first = self.object(0, 1)
        if u64(first, 40) != self.blocks:
            raise ValueError("APFS container size differs from its GPT partition")
        count, start = u32(first, 104), u64(first, 112)
        if not 0 < count <= 65536 or not 0 < start < start + count <= self.blocks:
            raise ValueError("Unsupported APFS checkpoint descriptor layout")
        # Checkpoint blocks are in a circular descriptor area. Only the
        # valid NXSB records for this same container are candidates.
        self.superblock = first
        for index in range(start, start + count):
            block = self.read(index)
            if (block[32:40] == first[32:40] and block[72:88] == first[72:88] and
                    u32(block, 24) & 0xffff == 1 and u64(block, 16) > u64(self.superblock, 16) and
                    u64(block, 0) == block_checksum(block)):
                self.superblock = block
        self.xid = u64(self.superblock, 16)

    def read(self, address):
        if not 0 <= address < self.blocks:
            raise ValueError("APFS object is outside the container")
        return self.image.read(self.offset + BLOCK * address, BLOCK)

    def object(self, address, kind):
        block = self.read(address)
        if (not u64(block, 0) or u64(block, 0) != block_checksum(block) or
                u32(block, 24) & 0xffff not in (kind if isinstance(kind, tuple) else (kind,))):
            raise ValueError(f"Invalid APFS object at block {address:#x}")
        return block

    def tree(self, root, resolve, *, subtype, fixed=False):
        pending, seen = [root], set()
        while pending:
            address = resolve(pending.pop())
            if address in seen:
                raise ValueError("APFS tree contains an object cycle")
            seen.add(address)
            data = self.object(address, (2, 3))
            if u32(data, 28) & 0xffff != subtype:
                raise ValueError("Unexpected APFS tree type")
            _, _, leaf, entries = node_records(data, fixed=fixed)
            if not leaf:
                pending.extend(u64(e.value, 0) for e in reversed(entries))
            yield Node(address, data, leaf, entries)

    def object_map(self, address, xid, *, headerless=False):
        omap = self.object(address, 11)
        mapping = {}
        for node in self.tree(u64(omap, 48), int, subtype=11, fixed=True):
            if not node.leaf:
                continue
            for entry in node.entries:
                oid, when = struct.unpack("<QQ", entry.key)
                if when <= xid and (oid not in mapping or when > mapping[oid][0]):
                    flags, size, block = struct.unpack("<IIQ", entry.value)
                    mapping[oid] = (when, flags, size, block)

        def lookup(oid):
            _, flags, size, block = mapping[oid]
            if flags & 5 or bool(flags & 8) != headerless or size != BLOCK:
                raise ValueError(f"APFS object {oid:#x} does not have the requested metadata layout")
            return block
        return lookup

    def volumes(self):
        resolve = self.object_map(u64(self.superblock, 160), self.xid)
        for index in range(100):
            oid = u64(self.superblock, 184 + index * 8)
            if oid:
                yield Volume(self, self.object(resolve(oid), 13))


class Volume:
    def __init__(self, container, superblock):
        self.container = container
        self.superblock = superblock
        if superblock[32:36] != b"APSB":
            raise ValueError("Missing APFS volume signature")
        self.uuid = UUID(bytes=superblock[240:256])
        self.role, = struct.unpack_from("<H", superblock, 0x3c4)
        self.name = superblock[0x2c0:0x3c0].split(b"\0", 1)[0].decode("utf-8")

    @property
    def sealed(self):
        return bool(u64(self.superblock, 56) & 0x20)

    @property
    def snapshot_count(self):
        return u64(self.superblock, 216)

    def signed_root_snapshot(self, expected_hash):
        """Match the stored Big Sur seal and return its real snapshot UUID.

        This validates the identity records, not a full traversal of the
        Merkle tree. It never creates or modifies a snapshot.
        """
        return self.signed_snapshot_record(expected_hash)[0]

    def signed_snapshot_record(self, expected_hash):
        if not self.sealed or self.snapshot_count != 1 or len(expected_hash) != 32:
            raise ValueError("Expected a single sealed APFS System snapshot")
        sb = self.superblock
        meta = self.container.object(self.resolve(u64(sb, 0x400)), 0x1e)
        version, flags, algorithm, offset, broken = struct.unpack_from("<IIIIQ", meta, 32)
        if (version not in (1, 2) or flags or broken or algorithm != 1 or not 56 <= offset <= BLOCK - 32 or
                meta[offset:offset + 32] != expected_hash):
            raise ValueError("The APFS seal does not match Apple's Intel root hash")
        extension = self.container.object(self.resolve(u64(sb, 0x3e8)), 0x1d)
        xid = u64(extension, 40)
        snapshot = UUID(bytes=extension[48:64])
        expected_name = b"com.apple.os.update-" + expected_hash.hex().upper().encode() + b"\0"
        records = [entry.value for node in self.container.tree(u64(sb, 152), int, subtype=16) if node.leaf
                   for entry in node.entries if u64(entry.key, 0) == (1 << 60) | xid]
        if (u32(extension, 32) != 1 or not xid or not snapshot.int or len(records) != 1 or
                records[0][50:] != expected_name):
            raise ValueError("The APFS System snapshot does not match its seal identity")
        return snapshot, xid, u64(records[0], 8)

    def authenticated_snapshot(self, expected_hash):
        """Return an iterator that authenticates fsroot metadata as it is consumed."""
        snapshot, xid, address = self.signed_snapshot_record(expected_hash)
        sb = self.container.object(address, 13)
        if (sb[32:36] != b"APSB" or sb[240:256] != self.uuid.bytes or not u64(sb, 56) & 0x20 or
                u64(sb, 16) != xid or not u64(sb, 136)):
            raise ValueError("Unexpected sealed snapshot superblock")
        resolve = self.container.object_map(u64(self.superblock, 128), xid, headerless=True)
        return snapshot, sb, hashed_tree(self.container.read, resolve, u64(sb, 136), expected_hash)

    @cached_property
    def resolve(self):
        return self.container.object_map(u64(self.superblock, 128), u64(self.superblock, 16))

    def nodes(self):
        if self.sealed:
            raise ValueError("This reader does not handle sealed APFS trees")
        return self.container.tree(u64(self.superblock, 136), self.resolve, subtype=14)

    def directory_entries(self):
        # Apple File System Reference: j_drec_key_t / j_drec_hashed_key_t,
        # j_drec_val_t, and the low nibble of dir_rec_flags.
        hashed = bool(u64(self.superblock, 56) & (0x01 | 0x08))
        name_offset = 12 if hashed else 10
        for node in self.nodes():
            if not node.leaf:
                continue
            for entry in node.entries:
                record = u64(entry.key, 0)
                if record >> 60 != DREC:
                    continue
                size = (u32(entry.key, 8) & 0x3ff) if hashed else struct.unpack_from("<H", entry.key, 8)[0]
                name = entry.key[name_offset:]
                if not name or size != len(name) or name[-1:] != b"\0" or b"/" in name:
                    raise ValueError("Invalid APFS directory-entry name")
                oid, _, flags = struct.unpack_from("<QQH", entry.value)
                yield record & OID_MASK, name[:-1], oid, flags & 0xf

    def read_small_file(self, relative, *, limit=65536):
        """Read an uncompressed, non-sparse control file from a detached volume."""
        names = relative.split(b"/")
        directories = {(parent, name): (inode, kind) for parent, name, inode, kind in self.directory_entries()}
        parent = 2
        for i, name in enumerate(names):
            parent, kind = directories[parent, name]
            if kind != (8 if i == len(names) - 1 else 4):
                raise ValueError("APFS control-file path is not a regular file")
        inode, extents = None, {}
        for node in self.nodes():
            if not node.leaf:
                continue
            for entry in node.entries:
                tag = u64(entry.key, 0)
                if tag == ((3 << 60) | parent):
                    inode = entry.value
                elif tag >> 60 == 8:
                    extents.setdefault(tag & OID_MASK, []).append((u64(entry.key, 8), entry.value))
        if inode is None or len(inode) < 96:
            raise ValueError("APFS control file does not have a data stream")
        fields, _ = struct.unpack_from("<HH", inode, 92)
        offset = 96 + 4 * fields
        size = None
        for i in range(fields):
            field, _, length = struct.unpack_from("<BBH", inode, 96 + 4 * i)
            if field == 8 and length == 40:
                size = u64(inode, offset)
                break
            offset += (length + 7) & ~7
        if size is None or not 0 <= size <= limit:
            raise ValueError("APFS control file exceeds the supported data stream size")
        data = bytearray()
        for logical, record in sorted(extents.get(u64(inode, 8), ())):
            length, physical, crypto = struct.unpack("<QQQ", record)
            if (logical != len(data) or crypto or not 0 < physical <= self.container.blocks - length // BLOCK
                    or not 0 < length <= (limit + BLOCK - 1) // BLOCK * BLOCK or length % BLOCK):
                raise ValueError("Unsupported APFS control file extent")
            data += self.container.image.read(self.container.offset + physical * BLOCK, min(length, size - len(data)))
            if len(data) == size:
                return bytes(data)
        if size:
            raise ValueError("APFS control file data is incomplete")
        return b""

    def firmlinks(self):
        attributes, inodes = {}, {}
        for node in self.nodes():
            if not node.leaf:
                continue
            for entry in node.entries:
                record = u64(entry.key, 0)
                kind, oid = record >> 60, record & OID_MASK
                if kind == INODE and u32(entry.value, 68) & SF_FIRMLINK:
                    inodes[oid] = struct.unpack_from("<Q", entry.value, 48)[0]
                if kind == XATTR and entry.key[10:] == FIRMLINK:
                    flags, size = struct.unpack_from("<HH", entry.value)
                    if len(entry.value) != size + 4:
                        raise ValueError("The APFS firmlink attribute has an invalid value size")
                    attributes[oid] = (flags, entry.value[4:])
        return inodes, attributes


def finalize_firmlinks(image_path, system_uuid, destinations):
    """Finalize existing reserved slots on an unpublished System volume.

    destinations maps the staging directory's APFS inode to the expected
    NUL-terminated Data-relative path bytes. No allocation, tree expansion,
    mounted guest disk, snapshots, or privileged host firmlink ioctl.
    """
    image_path = Path(image_path)
    if not destinations or attached_images(image_path):
        raise ValueError("Firmlinks need reserved slots on a detached new disk")
    image = detached_image(image_path)
    apfs = APFS(image)
    volume, = (v for v in apfs.volumes() if v.role == 1 and v.uuid == UUID(str(system_uuid)))
    if volume.snapshot_count:
        raise ValueError("Cannot finalize firmlinks on a snapshotted volume")
    marked, attributes, edits = set(), set(), {}
    for node in volume.nodes():
        block = None
        for entry in node.entries:
            record = u64(entry.key, 0)
            kind, oid = record >> 60, record & OID_MASK
            if oid not in destinations:
                continue
            if kind == INODE and node.leaf:
                mode, = struct.unpack_from("<H", entry.value, 80)
                flags = u32(entry.value, 68)
                if not stat.S_ISDIR(mode) or flags & SF_FIRMLINK or oid in marked:
                    raise ValueError(f"Unexpected inode for reserved firmlink {oid:#x}")
                if block is None:
                    block = bytearray(node.data)
                struct.pack_into("<I", block, entry.value_offset + 68, flags | SF_FIRMLINK)
                marked.add(oid)
            if kind != XATTR:
                continue
            name = entry.key[10:]
            if PLACEHOLDER < name <= FIRMLINK:
                raise ValueError("A reserved firmlink cannot be renamed in this tree")
            if name != PLACEHOLDER:
                continue
            # A separator can repeat the leaf's minimum key. Rename both.
            if node.leaf:
                destination = destinations[oid]
                if (oid in attributes or entry.value != struct.pack("<HH", 2, len(destination)) + destination):
                    raise ValueError(f"Unexpected firmlink attribute for inode {oid:#x}")
                attributes.add(oid)
            if block is None:
                block = bytearray(node.data)
            start = entry.key_offset + 10
            block[start:start + len(FIRMLINK)] = FIRMLINK
        if block is not None:
            struct.pack_into("<Q", block, 0, block_checksum(block))
            edits[node.address] = block
    if marked != destinations.keys() or attributes != marked:
        raise ValueError("The active APFS tree is missing one or more reserved firmlinks")
    # Nothing is written until all target inodes and attributes were found.
    for address, block in edits.items():
        image.write(apfs.offset + BLOCK * address, block)
