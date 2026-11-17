"""Plan a detached APFS checkpoint for the host-built Big Sur installer."""

from contextlib import ExitStack
from dataclasses import dataclass, replace
from itertools import groupby
import os
import stat
import struct

from host_install_apfs_records import BLOCK, block_checksum, u32, u64


CPM_ENTRY = struct.Struct("<4I3Q")
RINGS = struct.Struct("<IIQQ6I")
CHUNK = struct.Struct("<QQIIQ")
CPM_CAPACITY = (BLOCK - 40) // CPM_ENTRY.size


def checksummed(block):
    struct.pack_into("<Q", block, 0, block_checksum(block))
    return bytes(block)


def write_bundle_blocks(disk, offset, writes):
    """Write a checkpoint only into an unpublished, detached disk image."""
    with ExitStack() as stack:
        fds = {}
        for address, block in writes.items():
            band, position, count = next(disk.segments(offset + address * BLOCK, BLOCK))
            if count != BLOCK or len(block) != BLOCK:
                raise ValueError("Invalid APFS sparsebundle block write")
            if band not in fds:
                create = os.O_CREAT if band != disk.path else 0
                fd = os.open(band, os.O_RDWR | create | os.O_NOFOLLOW, 0o600)
                stack.callback(os.close, fd)
                if not stat.S_ISREG(os.fstat(fd).st_mode):
                    raise ValueError("Sparsebundle bands must be ordinary files")
                fds[band] = fd
            if os.pwrite(fds[band], block, position) != BLOCK:
                raise OSError("Short write to scratch APFS sparsebundle")
        for fd in fds.values():
            os.fsync(fd)


def window(start, length, capacity):
    return [(start + i) % capacity for i in range(length)]


def next_window(next_index, length, capacity, active):
    if not 0 <= next_index < capacity or not 0 < length <= capacity:
        raise ValueError("Invalid APFS checkpoint ring window")
    # Keep each window contiguous. Never overwrite the active checkpoint.
    start = next_index if next_index + length <= capacity else 0
    new = window(start, length, capacity)
    if set(new).intersection(active):
        raise ValueError("Insufficient APFS checkpoint ring space")
    return new


@dataclass(frozen=True)
class Mapping:
    kind: int
    subtype: int
    size: int
    pad: int
    filesystem: int
    oid: int
    physical: int

    def pack(self):
        return CPM_ENTRY.pack(self.kind, self.subtype, self.size, self.pad,
                              self.filesystem, self.oid, self.physical)


class Checkpoint:
    """Read the current checkpoint and stage a successor without writing the disk."""

    def __init__(self, container):
        self.container = container
        sb = self.superblock = container.superblock
        self.xid = u64(sb, 96)
        if self.xid <= container.xid:
            raise ValueError("Invalid next APFS transaction ID")
        (self.desc_count, self.data_count, self.desc_base, self.data_base, self.desc_next, self.data_next,
         desc_index, desc_length, data_index, data_length) = RINGS.unpack_from(sb, 104)
        if not (0 <= desc_index < self.desc_count and 2 <= desc_length <= self.desc_count
                and 0 <= data_index < self.data_count and 0 < data_length <= self.data_count
                and self.data_base + self.data_count <= container.blocks):
            raise ValueError("Invalid active APFS checkpoint geometry")
        self.desc_active = window(desc_index, desc_length, self.desc_count)
        self.data_active = window(data_index, data_length, self.data_count)
        self.mappings = {}
        source_blocks = set(self.data_base + i for i in self.data_active)
        for n, slot in enumerate(self.desc_active[:-1]):
            block = container.object(self.desc_base + slot, 12)
            flags, count = struct.unpack_from("<II", block, 32)
            if u64(block, 16) != container.xid or flags != int(n == desc_length - 2) or count > CPM_CAPACITY:
                raise ValueError("Invalid APFS checkpoint map")
            for i in range(count):
                item = Mapping(*CPM_ENTRY.unpack_from(block, 40 + i * CPM_ENTRY.size))
                key = item.filesystem, item.oid
                if item.size != BLOCK or item.physical not in source_blocks or key in self.mappings:
                    raise ValueError("Unsupported APFS checkpoint object mapping")
                source_blocks.remove(item.physical)
                self.mappings[key] = item
        descriptor = container.object(self.desc_base + self.desc_active[-1], 1)
        # The anchor and ring superblocks can differ in accounting counters.
        if source_blocks or descriptor[16:24] != sb[16:24] or descriptor[32:0x3d8] != sb[32:0x3d8]:
            raise ValueError("Incomplete APFS checkpoint")

    def read_ephemeral(self, oid, filesystem=0):
        item = self.mappings[filesystem, oid]
        block = self.container.object(item.physical, item.kind & 0xffff)
        if struct.unpack_from("<QQII", block, 8) != (item.oid, self.container.xid, item.kind, item.subtype):
            raise ValueError("APFS checkpoint object does not match its mapping")
        return block

    def blocks(self, *, ephemeral=None, discard=(), superblock=None):
        """Yield data slots, checkpoint map, then both copies of the new superblock."""
        ephemeral = {} if ephemeral is None else ephemeral
        discard = set(discard)
        if discard & ephemeral.keys() or discard - self.mappings.keys():
            raise ValueError("Invalid APFS checkpoint object removal")
        items = {key: item for key, item in self.mappings.items() if key not in discard}
        for key, data in ephemeral.items():
            oid, _, kind, subtype = struct.unpack_from("<QQII", data, 8)
            if len(data) != BLOCK or key[1] != oid or kind >> 30 != 2:
                raise ValueError("Invalid APFS ephemeral object")
            if key not in items:
                items[key] = Mapping(kind, subtype, BLOCK, 0, *key, 0)
            elif (items[key].kind, items[key].subtype) != (kind, subtype):
                raise ValueError("APFS replacement changed an ephemeral object's type")
        data_slots = next_window(self.data_next, len(items), self.data_count, self.data_active)
        groups = (len(items) + CPM_CAPACITY - 1) // CPM_CAPACITY
        desc_slots = next_window(self.desc_next, groups + 1, self.desc_count, self.desc_active)
        updated = []
        for item, slot in zip(items.values(), data_slots):
            key = item.filesystem, item.oid
            block = bytearray(ephemeral[key] if key in ephemeral else self.read_ephemeral(item.oid, item.filesystem))
            struct.pack_into("<Q", block, 16, self.xid)
            destination = self.data_base + slot
            yield destination, checksummed(block)
            updated.append(replace(item, physical=destination))
        for group in range(groups):
            records = updated[group * CPM_CAPACITY:(group + 1) * CPM_CAPACITY]
            address = self.desc_base + desc_slots[group]
            block = bytearray(BLOCK)
            struct.pack_into("<QQIIII", block, 8, address, self.xid, 0x4000000c, 0,
                             int(group + 1 == groups), len(records))
            raw = b"".join(item.pack() for item in records)
            block[40:40 + len(raw)] = raw
            yield address, checksummed(block)
        block = bytearray(self.superblock if superblock is None else superblock)
        struct.pack_into("<Q", block, 16, self.xid)
        struct.pack_into("<Q", block, 96, self.xid + 1)
        struct.pack_into("<6I", block, 128, (desc_slots[-1] + 1) % self.desc_count,
                         (data_slots[-1] + 1) % self.data_count,
                         desc_slots[0], len(desc_slots), data_slots[0], len(data_slots))
        final = checksummed(block)
        yield self.desc_base + desc_slots[-1], final
        yield 0, final


@dataclass(frozen=True)
class Chunk:
    xid: int
    first: int
    count: int
    free: int
    bitmap: int


def clear_runs(bitmap, count):
    """Yield (offset, length) of free bitmap runs, low bit first as in APFS."""
    if not 0 < count <= len(bitmap) * 8:
        raise ValueError("Invalid APFS chunk bitmap width")
    allocated = int.from_bytes(bitmap, "little")
    available = ((1 << count) - 1) & ~allocated
    while available:
        start = (available & -available).bit_length() - 1
        shifted = available >> start
        length = ((shifted ^ (shifted + 1)).bit_length() - 1)
        yield start, length
        available ^= ((1 << length) - 1) << start


class SpaceMap:
    """Read block availability from native APFS, not sparse-file holes."""

    def __init__(self, checkpoint):
        self.container = checkpoint.container
        self.oid = u64(checkpoint.superblock, 152)
        self.spaceman = sm = checkpoint.read_ephemeral(self.oid)
        size, self.blocks_per_chunk, _, _ = struct.unpack_from("<4I", sm, 32)
        blocks, chunks, cibs, cabs, self.free, offset = struct.unpack_from("<QQIIQI", sm, 48)
        if size != BLOCK or blocks != self.container.blocks or cabs or u64(sm, 96) or not cibs:
            raise ValueError("Unsupported APFS spaceman device geometry")
        if not 0 < self.blocks_per_chunk <= BLOCK * 8 or not 0 < offset <= BLOCK - cibs * 8:
            raise ValueError("Invalid APFS spaceman chunk table")
        self.cibs = struct.unpack_from(f"<{cibs}Q", sm, offset)
        self.chunks, self.chunk_offsets = [], []
        end = 0
        for i, physical in enumerate(self.cibs):
            cib = self.container.object(physical, 7)
            count = u32(cib, 36) & 0xfffff
            if u32(cib, 32) != i or not 0 < count <= (BLOCK - 40) // CHUNK.size:
                raise ValueError("Invalid APFS chunk-info block")
            for j in range(count):
                chunk = Chunk(*CHUNK.unpack_from(cib, 40 + j * CHUNK.size))
                if chunk.first != end or not 0 <= chunk.free <= chunk.count <= self.blocks_per_chunk:
                    raise ValueError("Invalid APFS chunk range")
                end += chunk.count
                self.chunks.append(chunk)
                self.chunk_offsets.append((i, 40 + j * CHUNK.size))
        if end != blocks or len(self.chunks) != chunks or sum(c.free for c in self.chunks) != self.free:
            raise ValueError("APFS spaceman totals do not match the chunk table")

    def free_runs(self):
        """Read and reconcile each chunk's allocation bitmap before using it."""
        previous = None
        for chunk in self.chunks:
            if chunk.bitmap:
                ranges = list(clear_runs(self.container.read(chunk.bitmap), chunk.count))
                if sum(length for _, length in ranges) != chunk.free:
                    raise ValueError(f"APFS free block count differs from bitmap at {chunk.first:#x}")
            elif chunk.free == chunk.count:
                ranges = [(0, chunk.count)]
            else:
                raise ValueError("Non-empty APFS chunk is missing its bitmap")
            for local, count in ranges:
                start = chunk.first + local
                if previous is not None:
                    if previous[0] + previous[1] == start:
                        previous = previous[0], previous[1] + count
                        continue
                    yield previous
                previous = start, count
        if previous is not None:
            yield previous

    def allocation(self, count):
        """Plan a contiguous reservation in a copy-on-write allocation transaction."""
        if count <= 0:
            raise ValueError("APFS allocation must request at least one block")
        start, length = max(self.free_runs(), key=lambda pair: pair[1], default=(0, 0))
        if not start or length < count:
            raise ValueError("Not enough contiguous space in APFS")
        plan = Allocation(self, start, count)
        plan.populate()
        return plan


class Allocation:
    """Stage new bitmaps and CIBs; the caller queues retirement and publishes."""

    def __init__(self, space, start, count):
        self.space, self.start, self.count = space, start, count
        self.spaceman = bytearray(space.spaceman)
        self.writes, self.retired_ip = {}, []
        self.xid = u64(space.container.superblock, 96)

    def populate(self):
        sm, volume = self.spaceman, self.space.container
        end = self.start + self.count
        if self.start <= 0 or end > volume.blocks:
            raise ValueError("Not enough contiguous space in APFS")
        pool_blocks, width, slots, ring, base = struct.unpack_from("<QIIQQ", sm, 152)
        head, tail, xid_off, index_off, next_off = struct.unpack_from("<HHIII", sm, 320)
        if (width != 1 or pool_blocks > BLOCK * 8 or not 0 <= head < slots or not 0 <= tail < slots
                or head == tail or not 0 < xid_off <= BLOCK - 8 or not 0 < index_off <= BLOCK - 2
                or not 0 < next_off <= BLOCK - 2 * slots):
            raise ValueError("Unsupported APFS internal-pool bitmap layout")
        active, = struct.unpack_from("<H", sm, index_off)
        following, = struct.unpack_from("<H", sm, next_off + head * 2)
        if active in (head, tail) or active >= slots or following >= slots:
            raise ValueError("Invalid APFS internal-pool bitmap ring")
        pool = int.from_bytes(volume.read(ring + active), "little")
        next_ip = 0

        def allocate_ip(old):
            nonlocal pool, next_ip
            free = ((1 << pool_blocks) - 1) & ~pool & ~((1 << next_ip) - 1)
            if not free:
                raise ValueError("Out of APFS internal-pool blocks")
            next_ip = (free & -free).bit_length() - 1
            if old:
                if not base <= old < base + pool_blocks or not (pool >> (old - base)) & 1:
                    raise ValueError("Invalid APFS internal-pool retirement")
                self.retired_ip.append(old)
            pool |= 1 << next_ip
            return base + next_ip

        chunks = ((pair, chunk) for pair, chunk in zip(self.space.chunk_offsets, self.space.chunks)
                  if chunk.first < end and chunk.first + chunk.count > self.start)
        cib_offset = u32(sm, 80)
        for i, group in groupby(chunks, lambda item: item[0][0]):
            original = self.space.cibs[i]
            cib = bytearray(volume.object(original, 7))
            for (_, offset), chunk in group:
                first = max(self.start, chunk.first) - chunk.first
                count = min(end, chunk.first + chunk.count) - chunk.first - first
                bitmap = int.from_bytes(volume.read(chunk.bitmap), "little") if chunk.bitmap else 0
                mask = ((1 << count) - 1) << first
                if bitmap & mask or count > chunk.free:
                    raise ValueError("Not enough contiguous space in APFS")
                address = allocate_ip(chunk.bitmap)
                self.writes[address] = (bitmap | mask).to_bytes(BLOCK, "little")
                CHUNK.pack_into(cib, offset, self.xid, chunk.first, chunk.count, chunk.free - count, address)
            # Reserve the CIB's new address before filling its physical header.
            address = allocate_ip(original)
            struct.pack_into("<QQ", cib, 8, address, self.xid)
            self.writes[address] = checksummed(cib)
            struct.pack_into("<Q", sm, cib_offset + i * 8, address)
        self.writes[ring + head] = pool.to_bytes(BLOCK, "little")
        struct.pack_into("<Q", sm, 72, self.space.free - self.count)
        struct.pack_into("<Q", sm, xid_off, self.xid)
        struct.pack_into("<H", sm, index_off, head)
        struct.pack_into("<HH", sm, 320, following, active)
        struct.pack_into("<H", sm, next_off + head * 2, 0xffff)
        struct.pack_into("<H", sm, next_off + tail * 2, active)
