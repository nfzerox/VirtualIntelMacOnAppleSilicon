"""Offline verification of Big Sur Intel data hashes against a published seal."""

from collections import defaultdict
from hashlib import sha256
import struct

from host_install_apfs_records import BLOCK, INODE, OID_MASK, XATTR, u64
from host_install_bigsur_cache_storage import extent_reader, require_stream_storage, verify_lzvn_fork


FILE_INFO = 13
OFFSET_MASK = (1 << 56) - 1
DECMPFS = b"com.apple.decmpfs\0"
RESOURCE = b"com.apple.ResourceFork\0"


def inode_stream(inode):
    """Read the APFS inode's optional j_dstream xfield."""
    if len(inode) < 96:
        return None
    count, _ = struct.unpack_from("<HH", inode, 92)
    cursor = 96 + count * 4
    if cursor > len(inode):
        raise ValueError("Invalid signed APFS inode xfield table")
    for index in range(count):
        kind, _, length = struct.unpack_from("<BBH", inode, 96 + 4 * index)
        if cursor + length > len(inode):
            raise ValueError("Invalid signed APFS inode xfield size")
        if kind == 8 and length == 40:
            return u64(inode, 8), u64(inode, cursor)
        cursor += (length + 7) & ~7
    return None


def verify_plain_stream(read, length, digests):
    total = (length + BLOCK - 1) & -BLOCK
    cursor = 0
    for offset, count, digest in sorted(digests):
        if offset != cursor or not BLOCK <= count <= min(4 * BLOCK, total - cursor) or count % BLOCK:
            raise ValueError("Signed digests do not cover the uncompressed stream")
        take = min(count, length - offset)
        data = read(offset, take)
        if len(data) != take or sha256(data.ljust(count, b"\0")).digest() != digest:
            raise ValueError(f"Uncompressed stream data hash mismatch at {offset:#x}")
        cursor += count
    if not total or cursor != total:
        raise ValueError("Signed digests do not cover the uncompressed stream")


def verify_sealed_file_data(system, root_hash):
    """Read every signed data stream, not the unsigned physical map alone.

    Big Sur's original Intel image uses uncompressed streams and LZVN
    resource forks for hashed file data. Inline compression bytes are stored
    in the authenticated fsroot itself and need no external-data check.
    """
    _, frozen, nodes = system.authenticated_snapshot(root_hash)
    plain, compressed, forks, digests = {}, {}, {}, defaultdict(list)
    digest_count = 0
    for node in nodes:
        for row in node.entries if node.leaf else ():
            tag = u64(row.key, 0)
            kind, oid = tag >> 60, tag & OID_MASK
            if kind == INODE:
                stream = inode_stream(row.value)
                if stream is not None:
                    plain[stream[0]] = stream[1]
            elif kind == XATTR and row.key[10:] == DECMPFS:
                flags, size, magic, algorithm, logical = struct.unpack_from("<HH4sIQ", row.value)
                if flags & 3 != 2 or size != len(row.value) - 4 or magic != b"fpmc":
                    raise ValueError("Invalid signed Big Sur compression record")
                compressed[oid] = (algorithm, logical)
            elif kind == XATTR and row.key[10:] == RESOURCE and row.value[0] & 3 == 1:
                stream, length = struct.unpack_from("<QQ", row.value, 4)
                forks[stream] = (oid, length)
            elif kind == FILE_INFO:
                algorithm_offset = u64(row.key, 8)
                pages, width = struct.unpack_from("<HB", row.value)
                if algorithm_offset >> 56 != 1 or width != 32 or len(row.value) != 35 or not 1 <= pages <= 4:
                    raise ValueError("Unsupported signed Big Sur file digest")
                digests[oid].append((algorithm_offset & OFFSET_MASK, pages * BLOCK, row.value[3:]))
                digest_count += 1
    if not digests:
        raise ValueError("Big Sur's sealed filesystem has no file-data digests")

    extents = defaultdict(list)
    container = system.container
    for node in container.tree(u64(frozen, 0x408), int, subtype=31, fixed=True):
        for row in node.entries if node.leaf else ():
            stream, logical = struct.unpack("<QQ", row.key)
            if stream in digests:
                count, physical = struct.unpack("<QQ", row.value)
                extents[stream].append((logical, count, physical))
    if extents.keys() != digests.keys():
        raise ValueError("Big Sur's physical extent tree does not back every signed data stream")

    # Read in approximate physical order to avoid seeking across the whole
    # image for each file when the user's library is on a spinning disk.
    for stream in sorted(digests, key=lambda oid: min(span[2] for span in extents[oid])):
        try:
            owner, stored = forks.get(stream, (None, plain.get(stream)))
            if not stored:
                raise ValueError("No nonempty data stream in the signed inode catalog")
            require_stream_storage(container.image, container.offset, container.blocks, stored, extents[stream])
            read = extent_reader(container.image, container.offset, extents[stream])
            if owner in compressed:
                algorithm, logical = compressed[owner]
                if algorithm != 8:
                    raise ValueError(f"Unsupported external compression type {algorithm}")
                verify_lzvn_fork(read, stored, logical, digests[stream])
            else:
                verify_plain_stream(read, stored, digests[stream])
        except ValueError as error:
            raise ValueError(f"Big Sur signed data stream {stream:#x} failed: {error}") from error
    return {"VerifiedSignedStreams": len(digests), "VerifiedSignedDataDigests": digest_count}
