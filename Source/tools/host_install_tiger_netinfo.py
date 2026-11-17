"""Tiger offline account prototype; operates on a fresh Essentials.pkg seed.

The database format is described by Apple's netinfo-369.6 dsrecord/dsstore
sources. The credential layout is from DirectoryService-353.6's NetInfo
plugin. This module does not operate on the host's user database.
"""

from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
import secrets
import struct
from uuid import UUID


U32 = struct.Struct(">I")
INDEX = struct.Struct(">4I")
STRING = 6
RECORD = 256


def words(*values):
    return struct.pack(f">{len(values)}I", *values)


class Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.pos = 0

    def take(self, length):
        end = self.pos + length
        if end > len(self.data):
            raise ValueError("Truncated NetInfo record")
        data = self.data[self.pos:end]
        self.pos = end
        return data

    def integer(self):
        return U32.unpack(self.take(4))[0]

    def atom(self):
        kind, length = self.integer(), self.integer()
        return kind, bytes(self.take(length))

    def attributes(self):
        return [(self.atom(), [self.atom() for _ in range(self.integer())])
                for _ in range(self.integer())]


def encode_atom(atom):
    kind, value = atom
    return words(kind, len(value)) + value


def encode_attributes(attributes):
    return words(len(attributes)) + b"".join(
        encode_atom(key) + words(len(values)) + b"".join(map(encode_atom, values))
        for key, values in attributes
    )


def string(value):
    # netinfod uses DataTypeCStr for NUL-terminated NetInfo property strings.
    return STRING, value.encode("utf-8") + b"\0"


@dataclass
class Record:
    identifier: int
    serial: int
    version: int
    parent: int
    children: list
    attributes: list
    metadata: list

    @classmethod
    def decode(cls, data):
        reader = Reader(data)
        header = [reader.integer() for _ in range(4)]
        children = [reader.integer() for _ in range(reader.integer())]
        record = cls(*header, children, reader.attributes(), reader.attributes())
        if reader.pos != len(data):
            raise ValueError("Unexpected trailing NetInfo record data")
        return record

    def encode(self):
        return (words(self.identifier, self.serial, self.version, self.parent,
                      len(self.children), *self.children)
                + encode_attributes(self.attributes) + encode_attributes(self.metadata))

    def get(self, key):
        return next((values for atom, values in self.attributes if atom == string(key)), [])

    def append(self, key, value):
        for atom, values in self.attributes:
            if atom == string(key):
                if string(value) not in values:
                    values.append(string(value))
                return
        self.attributes.append((string(key), [string(value)]))


def read_store(path):
    if (path / "Config").read_bytes() != words(1):
        raise ValueError("Unsupported NetInfo store version")
    records = {}
    stores = {}
    for identifier, version, size, index in INDEX.iter_unpack((path / "Index").read_bytes()):
        if size < 32 or size % 32 or identifier in records:
            raise ValueError("Invalid NetInfo index entry")
        if size not in stores:
            stores[size] = (path / f"Store.{size}").read_bytes()
        slot = stores[size][index * size:(index + 1) * size]
        kind, length = struct.unpack_from(">2I", slot)
        if len(slot) != size or kind != RECORD or length > size - 8:
            raise ValueError("Invalid indexed NetInfo record")
        record = Record.decode(slot[8:8 + length])
        if (record.identifier, record.version) != (identifier, version):
            raise ValueError("NetInfo index and store disagree")
        records[identifier] = record
    return records


def write_store(path, records):
    """Write a new store for an Intel guest; never overwrite an existing store."""
    stores = {}
    index = bytearray()
    for identifier, record in sorted(records.items()):
        payload = record.encode()
        entry = words(RECORD, len(payload)) + payload
        size = (len(entry) + 31) & ~31
        store = stores.setdefault(size, bytearray())
        index.extend(INDEX.pack(identifier, record.version, size, len(store) // size))
        store.extend(entry.ljust(size, b"\0"))

    path.mkdir(mode=0o700)
    (path / "Config").write_bytes(words(1))
    (path / "Index").write_bytes(index)
    for size, store in stores.items():
        (path / f"Store.{size}").write_bytes(store)
    checksum = sum((record.identifier + 1) * record.serial for record in records.values())
    # Unlike Index and Store, netinfod writes Clean in the guest's native order.
    (path / "Clean").write_bytes(struct.pack("<I", checksum & 0xFFFFFFFF))


def provision_account(records, name, full_name, guid, *, smb=False):
    """Add one account to an installer seed; return its assigned UID."""
    if not re.fullmatch(r"[a-z][a-z0-9_-]{0,30}", name) or not full_name or "\0" in full_name:
        raise ValueError("Invalid Tiger account name")
    guid = str(UUID(str(guid))).upper()

    def named(parent, name):
        return next((records[i] for i in parent.children
                     if string(name) in records[i].get("name")), None)

    users = named(records[0], "users")
    groups = named(records[0], "groups")
    admin = named(groups, "admin") if groups else None
    if users is None or admin is None:
        raise ValueError("Tiger seed is missing its users or administrator group")
    if any(value[1].lower() == string(name)[1] for identifier in users.children
           for value in records[identifier].get("name")):
        raise ValueError("Refusing to replace an existing Tiger account")
    used = {int(value[1].rstrip(b"\0")) for identifier in users.children
            for value in records[identifier].get("uid")}
    uid = next(candidate for candidate in range(501, 65534) if candidate not in used)
    identifier = max(records) + 1
    version = max(record.version for record in records.values())
    users.children.append(identifier)
    admin.append("users", name)
    admin.append("groupmembers", guid)
    for increment, record in enumerate((users, admin), start=1):
        record.serial += 1
        record.version = version + increment
    attributes = {
        "name": name, "realname": full_name, "uid": str(uid), "gid": "20",
        "home": f"/Users/{name}", "shell": "/bin/bash", "passwd": "********",
        "generateduid": guid, "authentication_authority":
            ";ShadowHash;HASHLIST:<SALTED-SHA1,SMB-NT>" if smb else ";ShadowHash;",
        "_writers_passwd": name, "_writers_picture": name,
    }
    records[identifier] = Record(identifier, 1, version + 3, users.identifier, [],
                                 [(string(k), [string(v)]) for k, v in attributes.items()], [])
    return uid


def shadow_hash(password, *, salt=None, smb=False):
    """Tiger's 620-byte ShadowHash with the standard salted-SHA1 slot."""
    if not password or b"\0" in password or len(password) > 511:
        raise ValueError("Invalid Tiger password")
    salt = secrets.token_bytes(4) if salt is None else salt
    if len(salt) != 4:
        raise ValueError("Tiger ShadowHash salts are four bytes")
    digest = hashlib.sha1(salt + password).digest()
    nt = bytes(16)
    if smb:
        # DirectoryService's NetInfo ShadowHash NT slot is bytes 0–15.
        # Use the Windows client's MD4(UTF-16LE password), not Tiger's
        # historical byte-wise Unicode conversion. Leave the LM slot empty.
        import ctypes
        md4 = ctypes.CDLL("/usr/lib/system/libcommonCrypto.dylib").CC_MD4
        md4.argtypes = (ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p)
        md4.restype = ctypes.c_void_p
        unicode_password = password.decode("utf-8").encode("utf-16-le")
        result = ctypes.create_string_buffer(16)
        if not md4(unicode_password, len(unicode_password), result):
            raise ValueError("Could not prepare the requested Tiger SMB credential")
        nt = result.raw
    # Tiger's DSUtils HexToBinaryConversion handles uppercase hex only.
    return (nt + bytes(68) + salt + digest + bytes(512)).hex().upper().encode("ascii")
