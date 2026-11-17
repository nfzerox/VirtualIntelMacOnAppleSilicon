"""Read the original Intel seal inputs without using a raw APFS device."""

import argparse
import ctypes
from functools import lru_cache
import json
from pathlib import Path
import re
import struct
import subprocess


OCTAL = re.compile(rb"\\([0-7]{3})")
ROOT_INODE = 2
DTYPE = {b"dir": 4, b"file": 8, b"link": 10}


def digest_records(stream):
    """Convert the original Berkeley DB records into Big Sur fsroot entries."""
    last = None
    while row := stream.read(52):
        if len(row) != 52:
            raise ValueError("Truncated Big Sur file digest")
        oid, offset, pages, digest_size = struct.unpack_from("<QQHB", row)
        key = oid, offset
        if not 0 < oid < 1 << 60 or offset >= 1 << 56 or not 0 < pages <= 4 or \
                digest_size != 32 or row[-1] != 0 or (last is not None and key <= last):
            raise ValueError("Unexpected Big Sur file digest record")
        last = key
        # Apple also stores supplemental (non-block-aligned) digests.
        # Only aligned file-data hashes become type-13 fsroot records.
        if offset % 4096:
            continue
        yield struct.pack("<QQ", (13 << 60) | oid, (1 << 56) | offset), row[16:51]


def exact(stream, count):
    data = stream.read(count)
    if len(data) != count:
        raise ValueError("Truncated Big Sur Intel seal asset")
    return data


def der_length(stream, tag):
    kind, length = exact(stream, 2)
    if kind != tag:
        raise ValueError("Unexpected Big Sur Intel seal container")
    if length < 128:
        return length
    width = length & 0x7f
    if not 0 < width <= 4:
        raise ValueError("Invalid Big Sur Intel seal container length")
    return int.from_bytes(exact(stream, width), "big")


@lru_cache(maxsize=1)
def lzfse_decoder():
    decode = ctypes.CDLL("/usr/lib/libcompression.dylib").compression_decode_buffer
    decode.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                       ctypes.c_void_p, ctypes.c_int)
    decode.restype = ctypes.c_size_t
    return decode


def decode_block(data, length):
    output = ctypes.create_string_buffer(length)
    if lzfse_decoder()(output, length, data, len(data), None, 0x801) != length:
        raise ValueError("Invalid LZFSE block in Big Sur's Intel seal archive")
    return output.raw


def decode_mtree(stream):
    """Yield the native Apple Archive stream in the Intel xmtr IMG4 payload."""
    sequence = der_length(stream, 0x30)
    end = stream.tell() + sequence
    for value in (b"IM4P", b"xmtr"):
        if der_length(stream, 0x16) != len(value) or exact(stream, len(value)) != value:
            raise ValueError("Not a Big Sur Intel Merkle tree archive")
    exact(stream, der_length(stream, 0x16))  # Description.
    length = der_length(stream, 4)
    if stream.tell() + length != end or length < 28:
        raise ValueError("Unexpected Big Sur Intel Merkle tree payload length")
    magic, maximum = struct.unpack(">4sQ", exact(stream, 12))
    if magic != b"pbze" or not 0 < maximum <= 16 * 2**20:
        raise ValueError("Unexpected Big Sur Intel Merkle tree compression")
    while stream.tell() < end:
        if end - stream.tell() < 16:
            raise ValueError("Truncated Big Sur Intel seal block header")
        size, stored = struct.unpack(">QQ", exact(stream, 16))
        if not 0 < stored <= size <= maximum or stored > end - stream.tell():
            raise ValueError("Unexpected Big Sur Intel seal block geometry")
        data = exact(stream, stored)
        yield data if size == stored else decode_block(data, size)
    if stream.read(1):
        raise ValueError("Trailing data after the Big Sur Intel Merkle tree")


def run_mtree(archive, member, *aa_arguments, output=None):
    command = ("/usr/bin/aa", *aa_arguments)
    with archive.open(member) as source, subprocess.Popen(command, stdin=subprocess.PIPE,
                                                         stdout=output) as process:
        try:
            with process.stdin as sink:
                for block in decode_mtree(source):
                    sink.write(block)
        except BaseException:
            if process.poll() is None:
                process.terminate()
            raise
        if result := process.wait():
            raise subprocess.CalledProcessError(result, command)


def mtree_entries(stream):
    """Yield (path components, attributes) from Apple's hierarchical mtree."""
    path, defaults, pending = (), {}, b""
    root = closed = False
    for line in stream:
        line = line.strip()
        if not line or line.startswith(b"#"):
            continue
        if closed:
            raise ValueError("Unexpected record after the Big Sur mtree root")
        if line.endswith(b"\\"):
            pending += line[:-1] + b" "
            continue
        name, *tokens = (pending + line).split()
        pending = b""
        if name == b"..":
            if not root or tokens:
                raise ValueError("Invalid directory close in Big Sur mtree")
            if path:
                path = path[:-1]
            else:
                closed = True
            continue
        if name == b"/unset":
            for key in tokens:
                if key == b"all":
                    defaults.clear()
                else:
                    defaults.pop(key, None)
            continue
        attributes = dict(token.split(b"=", 1) for token in tokens)
        if name == b"/set":
            defaults.update(attributes)
            continue
        attributes = {**defaults, **attributes}
        if name == b".":
            if root or path or attributes.get(b"type") != b"dir":
                raise ValueError("Invalid root entry in Big Sur mtree")
            root = True
            yield (), attributes
            continue
        name = OCTAL.sub(lambda match: bytes((int(match[1], 8),)), name)
        if not root or b"/" in name or b"\0" in name or name in (b"", b".", b".."):
            raise ValueError("Invalid filename in Big Sur mtree")
        absolute = (*path, name)
        yield absolute, attributes
        if attributes.get(b"type") == b"dir":
            path = absolute
    if pending or path or not closed:
        raise ValueError("Incomplete Big Sur mtree")


def compare_tree(volume, mtree):
    """Inventory the real System records against the authenticated path tree.

    An equal inventory is only a prerequisite; this does not compare file
    bytes, metadata normalization, or the final APFS seal.
    """
    remaining = {}
    for parent, name, oid, kind in volume.directory_entries():
        if (parent, name) in remaining:
            raise ValueError("Duplicate APFS directory entry")
        remaining[parent, name] = oid, kind
    dirs = {(): ROOT_INODE}
    result = {"Matched": 0, "Missing": 0, "WrongType": 0, "Unexpected": 0,
              "MissingExamples": [], "WrongTypeExamples": [], "UnexpectedExamples": []}

    def report(category, path):
        result[category] += 1
        examples = result[category + "Examples"]
        if len(examples) < 12:
            examples.append(b"/".join(path).decode("utf-8", "backslashreplace"))

    paths = {ROOT_INODE: ()}
    for path, attrs in mtree_entries(mtree):
        if not path:
            result["Matched"] += 1
            continue
        oid = dirs.get(path[:-1])
        record = remaining.pop((oid, path[-1]), None)
        if record is None:
            report("Missing", path)
            continue
        if record[1] != DTYPE[attrs[b"type"]]:
            report("WrongType", path)
            continue
        result["Matched"] += 1
        if record[1] == DTYPE[b"dir"]:
            dirs[path] = record[0]
            paths[record[0]] = path

    # APFS also stores private bookkeeping records outside the root hierarchy.
    # Count only extras reachable beneath the real root directory.
    deferred = {}
    for (parent, name), record in remaining.items():
        deferred.setdefault(parent, []).append((name, record))
    for parent, prefix in list(paths.items()):
        pending = [(parent, prefix)]
        while pending:
            inode, path = pending.pop()
            for name, (child, kind) in deferred.pop(inode, ()):
                extra = (*path, name)
                report("Unexpected", extra)
                if kind == DTYPE[b"dir"]:
                    pending.append((child, extra))
    return result


def extract_seal_inputs(archive, output, *, decoder=None):
    member, = (name for name in archive.namelist()
               if name.startswith("AssetData/boot/Firmware/") and name.endswith(".x86.mtree"))
    output.mkdir(mode=0o700)
    if decoder is not None:
        from host_install_bigsur_media import run_archive
        run_archive(archive, member, "--extract-intel-seal", output, decoder=decoder,
                    decoder_arguments=("--intel-mtree",), extractor=decoder)
    else:
        run_mtree(archive, member, "extract", "-d", output, "-exclude-field", "uid,gid,flg,acl")
    mtree, digest = output / "mtree.txt", output / "digest.db"
    if not (mtree.is_file() and digest.is_file()):
        raise ValueError("Apple's Intel seal archive is missing its tree or file digests")
    return mtree, digest


def main():
    from host_install_apfs import attached_images
    from host_install_apfs_records import APFS, SparseBundle
    from host_install_bigsur_media import signed_asset

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="Original Apple InstallAssistant.pkg")
    parser.add_argument("output", type=Path, help="New scratch directory for the original seal inputs")
    parser.add_argument("--compare", type=Path, metavar="SYSTEM.sparsebundle",
                        help="Compare the path inventory of a detached, unpublished System volume")
    args = parser.parse_args()
    with signed_asset(args.package) as (_, archive):
        mtree, digest = extract_seal_inputs(archive, args.output)
    report = {"Tree": str(mtree), "Digests": str(digest)}
    if args.compare:
        if attached_images(args.compare):
            raise ValueError("Detach the APFS scratch disk before comparing its System tree")
        volume, = (entry for entry in APFS(SparseBundle(args.compare)).volumes() if entry.role == 1)
        with mtree.open("rb") as stream:
            report["SystemPathInventory"] = compare_tree(volume, stream)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
