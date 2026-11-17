"""Retire Big Sur APFS metadata blocks in their next checkpoint, not in place."""

from itertools import chain
import struct

from host_install_apfs_records import BLOCK, fixed_table_size, u32, u64


FREE_QUEUE = 9
QUEUE_HEADER = struct.Struct("<QQQH")
GHOST = 0xffff
# Leave headroom for the root footer and use the same fanout at each level.
FANOUT = 120


def runs(rows):
    """Coalesce adjacent retired blocks in the same transaction."""
    last = None
    for xid, block, length in sorted(rows):
        if min(xid, block, length) <= 0:
            raise ValueError("Invalid APFS retirement range")
        if last and xid == last[0]:
            end = last[1] + last[2]
            if block < end:
                raise ValueError("Overlapping APFS retirement ranges")
            if block == end:
                last = xid, last[1], last[2] + length
                continue
        if last:
            yield last
        last = xid, block, length
    if last:
        yield last


def encode_node(oid, xid, level, records, totals=None):
    root = totals is not None
    block = bytearray(BLOCK)
    struct.pack_into("<QQII", block, 8, oid, xid, 0x80000002 if root else 0x80000003, FREE_QUEUE)
    table_size = fixed_table_size(16, 8)
    keys = 56 + table_size
    value_end = BLOCK - (40 if root else 0)
    values = value_end
    for i, (when, address, value) in enumerate(records):
        struct.pack_into("<QQ", block, keys + i * 16, when, address)
        offset = GHOST
        if level or value != 1:
            values -= 8
            struct.pack_into("<Q", block, values, value)
            offset = value_end - values
        struct.pack_into("<HH", block, 56 + i * 4, i * 16, offset)
    flags = 4 | (0 if level else 2) | int(root)
    key_size = len(records) * 16
    struct.pack_into("<HHI8H", block, 32, flags, level, len(records), 0, table_size,
                     key_size, values - keys - key_size, GHOST, 0, GHOST, 0)
    if root:
        # Sequential insertion, ghosts permitted, ephemeral children.
        struct.pack_into("<6I2Q", block, value_end, 0xe, BLOCK, 16, 8, 16, 8, *totals)
    return block


class FreeQueue:
    def __init__(self, checkpoint, spaceman, index):
        self.checkpoint = checkpoint
        self.offset = 200 + index * 40
        count, self.root, oldest, self.limit = QUEUE_HEADER.unpack_from(spaceman, self.offset)
        self.nodes, self.rows = set(), []
        if self.root:
            self.read(self.root, root=True)
        if count != sum(row[2] for row in self.rows) or oldest != (self.rows[0][0] if self.rows else 0):
            raise ValueError("APFS free queue totals do not match the tree")

    def read(self, oid, *, root=False):
        if oid in self.nodes:
            raise ValueError("Cycle in APFS free queue")
        self.nodes.add(oid)
        block = self.checkpoint.read_ephemeral(oid)
        flags, level, count, toc, length = struct.unpack_from("<HHIHH", block, 32)
        if (u32(block, 28) != FREE_QUEUE or flags & 7 != (4 | (0 if level else 2) | int(root))
                or count * 4 > length or (level and not count)):
            raise ValueError("Invalid APFS free queue node")
        keys, end = 56 + toc + length, BLOCK - (40 if root else 0)
        for i in range(count):
            key, value = struct.unpack_from("<HH", block, 56 + toc + i * 4)
            if not keys <= keys + key <= end - 16 or (value != GHOST and not 8 <= value <= end - keys):
                raise ValueError("Invalid APFS free queue record")
            when, address = struct.unpack_from("<QQ", block, keys + key)
            if level:
                if value == GHOST:
                    raise ValueError("Missing APFS free queue child")
                self.read(u64(block, end - value))
            else:
                self.rows.append((when, address, 1 if value == GHOST else u64(block, end - value)))

    def adding(self, spaceman, ranges, next_oid):
        """Return modified queue nodes, retired ephemeral IDs and the next available OID."""
        xid = self.checkpoint.xid
        rows = list(runs(chain(self.rows, ((xid, start, length) for start, length in ranges))))
        # Even across epochs, a physical block must not be in a queue twice.
        end = 0
        for _, start, length in sorted(rows, key=lambda row: row[1]):
            if start < end or start + length > self.checkpoint.container.blocks:
                raise ValueError("Invalid APFS free queue block range")
            end = start + length
        if not self.root or not self.limit:
            raise ValueError("Missing native APFS free queue")
        # The native internal-pool queue cannot grow an index. Use the
        # full fixed table there, taking one-block ghost values into account.
        root_capacity = FANOUT
        if self.limit == 1:
            table = fixed_table_size(16, 8)
            if len(rows) <= table // 4 and table + len(rows) * 16 + \
                    sum(8 for _, _, count in rows if count != 1) <= BLOCK - 56 - 40:
                root_capacity = max(root_capacity, len(rows))
        writes, level, items = {}, 0, rows
        reusable = iter(sorted(self.nodes - {self.root}))
        while len(items) > (FANOUT if level else root_capacity):
            parents = []
            for i in range(0, len(items), FANOUT):
                group = items[i:i + FANOUT]
                oid = next(reusable, None)
                if oid is None:
                    oid, next_oid = next_oid, next_oid + 1
                writes[0, oid] = encode_node(oid, xid, level, group)
                parents.append((*group[0][:2], oid))
            level, items = level + 1, parents
        nodes = len(writes) + 1
        if nodes > self.limit:
            raise ValueError(f"APFS free queue {self.root} exceeds its {self.limit}-node limit for {len(rows)} records")
        writes[0, self.root] = encode_node(self.root, xid, level, items, (len(rows), nodes))
        QUEUE_HEADER.pack_into(spaceman, self.offset, sum(row[2] for row in rows),
                               self.root, rows[0][0] if rows else 0, self.limit)
        return writes, {(0, oid) for oid in self.nodes} - writes.keys(), next_oid


def retirement_checkpoint(checkpoint, allocation, retired_main=(), *, superblock=None):
    """Combine the COW allocation map and delayed retirement at one commit boundary."""
    sm = bytearray(allocation.spaceman)
    sb = bytearray(checkpoint.superblock if superblock is None else superblock)
    next_oid = u64(sb, 88)
    replacements, discard = {(0, allocation.space.oid): sm}, set()
    for i, ranges in enumerate((((p, 1) for p in allocation.retired_ip), retired_main)):
        ranges = list(ranges)
        if ranges:
            queue = FreeQueue(checkpoint, sm, i)
            writes, drop, next_oid = queue.adding(sm, ranges, next_oid)
            replacements.update(writes)
            discard.update(drop)
    struct.pack_into("<Q", sb, 88, next_oid)
    yield from allocation.writes.items()
    yield from checkpoint.blocks(ephemeral=replacements, discard=discard, superblock=sb)
