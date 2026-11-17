"""Add the native Lion-style Apple_Boot slice to an owned HFS+ staging image."""

import struct
import uuid
import zlib

from host_install_hfs_metadata import SparseBundle


SECTOR = 512
RECOVERY_SECTORS = 1269536
HFS = uuid.UUID("48465300-0000-11aa-aa11-00306543ecac")
APPLE_BOOT = uuid.UUID("426f6f74-0000-11aa-aa11-00306543ecac")


def gpt_header(image, lba):
    block = bytearray(image.read(lba * SECTOR, SECTOR))
    signature, revision, size, expected = struct.unpack_from("<8sIII", block)
    if signature != b"EFI PART" or revision != 0x10000 or not 92 <= size <= SECTOR:
        raise ValueError("Unexpected GPT header")
    struct.pack_into("<I", block, 16, 0)
    if zlib.crc32(block[:size]) != expected:
        raise ValueError("Invalid GPT header checksum")
    current, other, first, last = struct.unpack_from("<QQQQ", block, 24)
    table, count, stride, checksum = struct.unpack_from("<QIII", block, 72)
    if current != lba or count != 128 or stride != 128:
        raise ValueError("Unexpected GPT geometry")
    entries = bytearray(image.read(table * SECTOR, count * stride))
    if zlib.crc32(entries) != checksum:
        raise ValueError("Invalid GPT partition table checksum")
    return block, size, other, first, last, table, entries


def add_recovery(image_path, recovery_path):
    """Copy a detached native recovery filesystem into the reserved GPT gap."""
    image, recovery = SparseBundle(image_path), SparseBundle(recovery_path)
    main = gpt_header(image, 1)
    backup = gpt_header(image, main[2])
    if backup[2] != 1 or main[3:5] != backup[3:5] or main[6] != backup[6]:
        raise ValueError("Primary and backup GPT do not match")
    entries = main[6]
    if entries[128:144] != HFS.bytes_le or any(entries[256:]):
        raise ValueError("Expected a native GPT with EFI and one HFS+ partition")
    start = struct.unpack_from("<Q", entries, 168)[0] + 1
    end = start + RECOVERY_SECTORS - 1
    if start % 8 or end > main[4] or main[4] - end != 7:
        raise ValueError("GPT has not reserved the exact native recovery gap")
    header = recovery.read(1024, SECTOR)
    if header[:4] != b"H+\0\x04" or not struct.unpack_from(">I", header, 4)[0] & 0x100:
        raise ValueError("Recovery volume is not a cleanly detached HFS+ image")
    size = RECOVERY_SECTORS * SECTOR
    block_size, blocks = struct.unpack_from(">II", header, 40)
    if block_size * blocks != size:
        raise ValueError("Recovery filesystem size does not match its partition")

    # Write only sparsebundle bands that physically exist. The reserved tail
    # is empty except for metadata from the original HFS shrink, so replace
    # existing destination bytes with zeroes even when the source is sparse.
    end_offset = start * SECTOR + size
    cursor = start * SECTOR
    while cursor < end_offset:
        offset = cursor - start * SECTOR
        count = min(recovery.band_size - offset % recovery.band_size, end_offset - cursor)
        source, within, _ = next(recovery.segments(offset, count))
        content = b""
        if source.is_file():
            with source.open("rb") as stream:
                stream.seek(within)
                content = stream.read(count)
        content = content.ljust(count, b"\0")
        for path, within, length in image.segments(cursor, count):
            fragment, content = content[:length], content[length:]
            if not path.exists() and fragment.count(0) == length:
                continue
            with path.open("r+b" if path.exists() else "xb") as stream:
                stream.seek(within)
                stream.write(fragment)
        cursor += count

    struct.pack_into("<16s16sQQQ", entries, 256, APPLE_BOOT.bytes_le,
                     uuid.uuid4().bytes_le, start, end, 0)
    label = "Recovery HD".encode("utf-16le")
    entries[312:312 + len(label)] = label
    for header, length, _, _, _, table, _ in (backup, main):
        struct.pack_into("<I", header, 88, zlib.crc32(entries))
        struct.pack_into("<I", header, 16, zlib.crc32(header[:length]))
        image.write(table * SECTOR, entries)
        image.write(struct.unpack_from("<Q", header, 24)[0] * SECTOR, header)
