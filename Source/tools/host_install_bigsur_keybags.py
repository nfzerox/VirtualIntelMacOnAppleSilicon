"""Decode Intel Big Sur keybag objects for a disposable, detached audit.

The installed disk is read-only. This is not FileVault recovery and it does
not unwrap any password-protected volume encryption keys.
"""

import ctypes
import struct

from host_install_apfs_records import BLOCK, block_checksum, u32, u64


# Apple File System Reference: nx_superblock_t, apfs_superblock_t and
# media_keybag_t. Big Sur's Intel keybag uses 512-byte XTS data units and
# the container-relative sector number. The two AES keys are its UUID.
NX_KEYLOCKER = 1296
NX_MKB_LOCKER = 1392
APFS_FS_FLAGS = 264
APFS_FS_UNENCRYPTED = 1
KEYBAG_CONTAINER = 0x6B657973
KEYBAG_VOLUME = 0x72656373
KEYBAG_UNLOCK_RECORDS = 3
SECTOR = 512


def aes_ecb(operation, key, data):
    aes = ctypes.CDLL("/usr/lib/libSystem.B.dylib").CCCrypt
    aes.argtypes = (ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
                    ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p,
                    ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p,
                    ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t))
    aes.restype = ctypes.c_int32
    output = ctypes.create_string_buffer(len(data))
    size = ctypes.c_size_t()
    # kCCAlgorithmAES128, kCCOptionECBMode; there is no padding.
    if aes(operation, 0, 2, key, len(key), None, data, len(data),
           output, len(data), ctypes.byref(size)) or size.value != len(data):
        raise ValueError("Could not decode a detached APFS keybag")
    return output.raw


def decrypt_keybag(data, address, uuid):
    if len(data) != BLOCK or len(uuid) != 16 or not 0 < address < (1 << 61):
        raise ValueError("Invalid APFS keybag block or UUID")
    first_sector = address * (BLOCK // SECTOR)
    tweaks = aes_ecb(0, uuid, b"".join(n.to_bytes(16, "little")
                     for n in range(first_sector, first_sector + BLOCK // SECTOR)))
    mask = bytearray()
    for offset in range(0, len(tweaks), 16):
        tweak = int.from_bytes(tweaks[offset:offset + 16], "little")
        for _ in range(SECTOR // 16):
            mask.extend(tweak.to_bytes(16, "little"))
            tweak = ((tweak << 1) & ((1 << 128) - 1)) ^ (0x87 if tweak >> 127 else 0)
    mixed = aes_ecb(1, uuid, bytes(a ^ b for a, b in zip(data, mask)))
    return bytes(a ^ b for a, b in zip(mixed, mask))


def keybag_entries(data):
    version, count, size = struct.unpack_from("<HHI", data, 32)
    if version != 2 or not 16 <= size <= BLOCK - 32 or count > (size - 16) // 24:
        raise ValueError("Invalid Big Sur APFS keybag length or version")
    offset = 48
    for _ in range(count):
        if offset + 24 > 32 + size:
            raise ValueError("Truncated Big Sur APFS keybag entry")
        uuid, tag, length = struct.unpack_from("<16sHH", data, offset)
        start = offset + 24
        offset = (start + length + 15) & ~15
        if offset > 32 + size:
            raise ValueError("Truncated Big Sur APFS keybag payload")
        yield uuid, tag, data[start:start + length]
    if offset != 32 + size:
        raise ValueError("Unexpected data at the end of the Big Sur APFS keybag")


def decoded_keybags(container):
    """Return only checksummed plaintext keybag blocks for the audit copy."""
    sb = container.superblock
    address, count = struct.unpack_from("<QQ", sb, NX_KEYLOCKER)
    if struct.unpack_from("<QQ", sb, NX_MKB_LOCKER) != (0, 0):
        raise ValueError("Detached Big Sur audit does not support media keybags")
    if not address and not count:
        return {}
    volumes = {v.uuid.bytes: v for v in container.volumes()}
    if not volumes or any(not u64(v.superblock, APFS_FS_FLAGS) & APFS_FS_UNENCRYPTED for v in volumes.values()):
        raise ValueError("Detached Big Sur keybag audit requires unencrypted APFS volumes")
    blocks = {}

    def read(block, length, uuid, kind):
        if length != 1 or not 0 < block < container.blocks or block in blocks:
            raise ValueError("Unsupported or duplicate Big Sur APFS keybag range")
        data = decrypt_keybag(container.read(block), block, uuid)
        if (u64(data, 0) != block_checksum(data) or u32(data, 24) != kind or u32(data, 28) or
                not 0 < u64(data, 16) <= container.xid):
            raise ValueError(f"Invalid Big Sur APFS keybag at block {block}")
        blocks[block] = data
        return list(keybag_entries(data))

    seen = set()
    for uuid, tag, payload in read(address, count, sb[72:88], KEYBAG_CONTAINER):
        if tag != KEYBAG_UNLOCK_RECORDS or uuid not in volumes or uuid in seen or len(payload) != 16:
            raise ValueError("Unexpected keybag reference in unencrypted Big Sur APFS")
        seen.add(uuid)
        block, length = struct.unpack("<QQ", payload)
        read(block, length, uuid, KEYBAG_VOLUME)
    return blocks
