"""Verify that the Intel dyld caches' sealed extents have physical backing."""

from bisect import bisect_right
from collections import defaultdict
import ctypes
from hashlib import sha256
import struct

from host_install_apfs_records import BLOCK, DREC, MappedQCOWImage, OID_MASK, XATTR, u64
from host_install_bigsur_seal import lzfse_decoder


CACHES = (b"dyld_shared_cache_x86_64", b"dyld_shared_cache_x86_64h")
CHUNK = 65536


def require_backed_range(image, offset, length):
    # A sparsebundle's implicit zeros are valid for unused blocks, not for
    # allocated LZVN resource fork bytes. A short band may still contain
    # valid APFS metadata, so fsck alone cannot catch this failure.
    if isinstance(image, MappedQCOWImage):
        # QEMU may deliberately encode zero padding as a hole. Its map
        # validates backing offsets; the signed digests below verify that
        # reading those ranges (including synthesized zero bytes) is correct.
        if not 0 <= offset <= offset + length <= image.size:
            raise ValueError("The allocated APFS file extent is outside the QCOW2 disk")
        return
    for band, begin, count in image.segments(offset, length):
        if not band.is_file() or band.stat().st_size < begin + count:
            raise ValueError(f"Backing file {band.name} does not store bytes {begin:#x}–{begin + count:#x}")


def require_stream_storage(image, container_offset, container_blocks, length, extents):
    cursor = 0
    for logical, count, physical in sorted(extents):
        if (logical != cursor or not count or count % BLOCK or
                not 0 < physical <= container_blocks - count // BLOCK):
            raise ValueError("Invalid or discontinuous APFS file extent")
        take = min(count, length - cursor)
        require_backed_range(image, container_offset + physical * BLOCK, take)
        cursor += take
        if cursor == length:
            return
    raise ValueError("Incomplete APFS file extent coverage")


def extent_reader(image, container_offset, extents):
    extents = sorted(extents)
    starts = [position for position, _, _ in extents]

    def read(position, length):
        data = bytearray()
        while length:
            logical, count, physical = extents[bisect_right(starts, position) - 1]
            offset = position - logical
            if not 0 <= offset < count:
                raise ValueError("Read outside the verified APFS file extent")
            take = min(length, count - offset)
            data.extend(image.read(container_offset + physical * BLOCK + offset, take))
            position, length = position + take, length - take
        return bytes(data)

    return read


def verify_lzvn_fork(read, length, logical_length, digests):
    """Compare decoded resource bytes to every authenticated APFS data digest."""
    if not 0 < logical_length <= 4 * 2**30:
        raise ValueError("Invalid LZVN logical stream length")
    chunks = (logical_length + CHUNK - 1) // CHUNK
    table_size = 4 * (chunks + 1)
    if table_size >= length:
        raise ValueError("Invalid LZVN resource fork length")
    offsets = struct.unpack(f"<{chunks + 1}I", read(0, table_size))
    if offsets[0] != table_size or offsets[-1] != length:
        raise ValueError("LZVN offsets do not cover the resource fork")
    expected = iter(sorted(digests))
    decoder = lzfse_decoder()
    output = ctypes.create_string_buffer(CHUNK + 1)
    for i, (start, end) in enumerate(zip(offsets, offsets[1:])):
        base = i * CHUNK
        size = min(CHUNK, logical_length - base)
        # The stored slot may retain zero padding after compression; the
        # final slot can be wider than its short logical tail.
        if not 0 < end - start <= CHUNK + 1:
            raise ValueError("Invalid LZVN chunk range")
        payload = read(start, end - start)
        if payload[0] == 6 and len(payload) == size + 1:
            data = payload[1:]
        else:
            # A resource-fork slot can have zero padding after LZVN's
            # eight-byte EOS. The raw decoder stops there, but the LZFSE
            # wrapper requires its payload length to be exactly consumed.
            # Trim only zero padding; the decoded bytes must still match
            # every signed APFS digest below.
            marker = len(payload.rstrip(b"\0"))
            if marker and payload[marker - 1] == 6 and marker + 7 < len(payload):
                payload = payload[:marker + 7]
            frame = struct.pack("<4sII", b"bvxn", size, len(payload)) + payload + b"bvx$"
            if decoder(output, size + 1, frame, len(frame), None, 0x801) != size:
                raise ValueError(f"LZVN chunk could not be decoded at {base:#x}")
            data = output.raw[:size]
        offset = 0
        while offset < size:
            position, span, digest = next(expected, (-1, 0, None))
            remaining = (size - offset + BLOCK - 1) & -BLOCK
            if position != base + offset or not 0 < span <= min(4 * BLOCK, remaining):
                raise ValueError("Signed digests do not cover the LZVN stream")
            # APFS seals the final partial page as if zero-padded to 4 KiB.
            if sha256(data[offset:offset + span].ljust(span, b"\0")).digest() != digest:
                raise ValueError(f"LZVN stream data hash mismatch at {position:#x}")
            offset += span
    if next(expected, None) is not None:
        raise ValueError("Unexpected data digests after the LZVN stream")


def require_sealed_cache_storage(system, root_hash):
    """Authenticate the exported bytes, not only the snapshot metadata."""
    _, frozen, nodes = system.authenticated_snapshot(root_hash)
    components = (b"System", b"Library", b"dyld", *CACHES)
    directories, resources = {}, {}
    for node in nodes:
        for entry in node.entries if node.leaf else ():
            tag = u64(entry.key, 0)
            kind, oid = tag >> 60, tag & OID_MASK
            if kind == DREC:
                name = entry.key[12:-1]  # Sealed Intel System uses hashed names.
                if name in components:
                    directories[oid, name] = u64(entry.value, 0)
            elif kind == XATTR and entry.key[10:] == b"com.apple.ResourceFork\0" and len(entry.value) == 52:
                if entry.value[0] & 3 == 1:
                    resources[oid] = struct.unpack_from("<QQ", entry.value, 4)
    cache_dir = 2
    for component in components[:3]:
        cache_dir = directories[cache_dir, component]
    owners = {name: directories[cache_dir, name] for name in CACHES}
    streams = {name: resources[owner] for name, owner in owners.items()}
    wanted = {stream: [] for stream, _ in streams.values()}
    container = system.container
    for node in container.tree(u64(frozen, 0x408), int, subtype=31, fixed=True):
        for entry in node.entries if node.leaf else ():
            stream, logical = struct.unpack("<QQ", entry.key)
            if stream in wanted:
                count, physical = struct.unpack("<QQ", entry.value)
                wanted[stream].append((logical, count, physical))
    for name, (stream, length) in streams.items():
        try:
            require_stream_storage(container.image, container.offset, container.blocks, length, wanted[stream])
        except ValueError as error:
            raise ValueError(f"Big Sur's {name.decode('ascii')} resource fork is incomplete: {error}") from error
    compression, digests = {}, defaultdict(list)
    _, _, nodes = system.authenticated_snapshot(root_hash)
    for node in nodes:
        for entry in node.entries if node.leaf else ():
            tag = u64(entry.key, 0)
            kind, oid = tag >> 60, tag & OID_MASK
            if kind == 13 and oid in wanted:
                algorithm_offset = u64(entry.key, 8)
                pages, width = struct.unpack_from("<HB", entry.value)
                if algorithm_offset >> 56 != 1 or width != 32 or len(entry.value) != 35:
                    raise ValueError("Unsupported signed Intel cache digest")
                digests[oid].append((algorithm_offset & ((1 << 56) - 1), pages * BLOCK, entry.value[3:]))
            elif kind == XATTR and oid in owners.values() and entry.key[10:] == b"com.apple.decmpfs\0":
                flags, size, magic, algorithm, logical = struct.unpack("<HH4sIQ", entry.value)
                if flags & 3 != 2 or size != 16 or magic != b"fpmc" or algorithm != 8:
                    raise ValueError("Unsupported Intel cache compression")
                compression[oid] = logical
    for name, (stream, length) in streams.items():
        try:
            read = extent_reader(container.image, container.offset, wanted[stream])
            verify_lzvn_fork(read, length, compression[owners[name]], digests[stream])
        except (KeyError, ValueError) as error:
            raise ValueError(f"Big Sur's {name.decode('ascii')} cache failed data verification: {error}") from error
