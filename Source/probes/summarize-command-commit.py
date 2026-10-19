#!/usr/bin/python
"""Summarize one captured AppleParavirtCommandBuffer without copying files."""
from __future__ import print_function

import collections
import glob
import os
import re
import struct
import sys
import zipfile


def field(line, name):
    match = re.search(r"(?:^|\s)" + re.escape(name) + r"=([^\s]+)", line)
    return match.group(1) if match else None


def main(trace_dir, sequence, archive_path=None):
    records = []
    resources = {}
    manifest_lines = []
    active = False
    input_archive = (zipfile.ZipFile(trace_dir, "r")
                     if trace_dir.endswith(".zip") else None)
    if input_archive:
        manifest_input = input_archive.read("manifest.txt").decode("utf-8").splitlines(True)
    else:
        with open(os.path.join(trace_dir, "manifest.txt"), "r") as manifest:
            manifest_input = manifest.readlines()
    for line in manifest_input:
            if line.startswith("commit sequence="):
                if active:
                    break
                active = field(line, "sequence") == str(sequence)
                if active:
                    print(line.rstrip())
                    manifest_lines.append(line)
            elif active and line.startswith("record sequence="):
                manifest_lines.append(line)
                records.append((int(field(line, "sequence")),
                                int(field(line, "opcode"), 16),
                                field(line, "path"),
                                int(field(line, "offset"))
                                if field(line, "offset") else None,
                                int(field(line, "size"))))
            elif active and line.startswith("reference sequence="):
                manifest_lines.append(line)
                ref = int(field(line, "serializer"), 16)
                if ref:
                    resources[ref] = (field(line, "class"),
                                      int(field(line, "length"), 16),
                                      field(line, "dimensions"),
                                      field(line, "format"),
                                      field(line, "type"))
    if not records:
        raise ValueError("commit not found or empty")
    records.sort()
    if archive_path and input_archive:
        raise ValueError("cannot archive an archive")
    if archive_path:
        if os.path.exists(archive_path):
            raise ValueError("archive exists: " + archive_path)
        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED,
                             allowZip64=True) as archive:
            archive.writestr("manifest.txt", "".join(manifest_lines))
            for path in sorted({item[2] for item in records}):
                archive.write(path, os.path.basename(path))
            for path in glob.glob(os.path.join(
                    trace_dir, "commit-%03d-buffer-*.bin" % sequence)):
                archive.write(path, os.path.basename(path))
        print("archive=%s bytes=%d" %
              (archive_path, os.path.getsize(archive_path)))
    print("records=%d resources=%d opcodes=%s" %
          (len(records), len(resources),
           sorted(collections.Counter(item[1] for item in records).items())))
    warnings = []
    indexed = []
    render_passes = []
    sidecars = 0
    vertex_buffers = {}
    fragment_buffers = {}
    buffer_offsets = []
    fragment_textures = {}
    vertex_textures = {}
    pipeline = None
    three_d_binds = []
    three_d_draws = []
    mipmap_generations = []
    record_file_cache = {}
    for number, listed_opcode, path, offset, size in records:
        if input_archive:
            archive_name = os.path.basename(path)
            if archive_name not in record_file_cache:
                record_file_cache[archive_name] = input_archive.read(
                    archive_name)
            data = record_file_cache[archive_name]
            if offset is not None:
                data = data[offset:offset + size]
        else:
            with open(path, "rb") as source:
                if offset is not None:
                    source.seek(offset)
                    data = source.read(size)
                else:
                    data = source.read()
        # The serializer requests one 8-byte opaque sidecar per render pass;
        # it has no opcode/length header and is not part of the GPU op stream.
        if len(data) == 8 and struct.unpack_from("<I", data, 4)[0] <= 5:
            sidecars += 1
            continue
        if len(data) < 8:
            warnings.append("%d short record" % number)
            continue
        opcode, wire_length = struct.unpack_from("<II", data)
        if opcode != listed_opcode or wire_length != len(data):
            warnings.append("%d bad header opcode=%#x length=%d actual=%d" %
                            (number, opcode, wire_length, len(data)))
        if opcode in (1, 6, 7):
            bound_3d = [(slot, ref) for slot, ref in fragment_textures.items()
                        if resources.get(ref, (None, None, None, None, None))[4]
                        == "0x7"]
            if bound_3d:
                three_d_draws.append((number, pipeline, bound_3d))
        if opcode in (6, 7):
            if opcode == 6 and len(data) >= 32:
                primitive, index_type, ref, count, offset = struct.unpack_from(
                    "<HHIQQ", data, 8)
            elif opcode == 7 and len(data) >= 20:
                primitive, index_type, ref, count, offset = struct.unpack_from(
                    "<HHIHH", data, 8)
            else:
                warnings.append("%d short indexed draw" % number)
                continue
            resource = resources.get(ref)
            end = offset + count * (2 if index_type == 0 else 4)
            indexed.append((number, ref, count, offset, end, resource))
            if (index_type not in (0, 1) or primitive not in range(1, 6)
                    or not resource or end > resource[1]):
                warnings.append("%d invalid indexed draw type=%d indexType=%d "
                                "buffer=%#x count=%d offset=%d end=%d resource=%r" %
                                (number, primitive, index_type, ref, count,
                                 offset, end, resource))
        elif opcode == 0x1a and len(data) >= 592:
            vertex_buffers = {}
            fragment_buffers = {}
            vertex_textures = {}
            fragment_textures = {}
            pipeline = None
            colors = []
            for slot in range(8):
                ref = struct.unpack_from("<I", data, 84 + slot * 60)[0]
                if ref:
                    colors.append((slot, ref, resources.get(ref)))
            width, height = struct.unpack_from("<QQ", data, 576)
            render_passes.append((number, width, height, colors))
            for _, ref, resource in colors:
                if not resource:
                    warnings.append("%d attachment %#x absent from refs" %
                                    (number, ref))
                elif resource[2] and "x" in resource[2]:
                    dimensions = [int(x) for x in resource[2].split("x")]
                    if width > dimensions[0] or height > dimensions[1]:
                        warnings.append("%d pass %dx%d exceeds attachment %#x %s" %
                                        (number, width, height, ref, resource[2]))
        elif opcode in (0x7d, 0x6e) and len(data) >= 16:
            first, count = struct.unpack_from("<II", data, 8)
            state = vertex_buffers if opcode == 0x7d else fragment_buffers
            if 16 + count * 12 > len(data):
                warnings.append("%d short buffer bind" % number)
                continue
            for slot in range(count):
                ref, offset = struct.unpack_from("<IQ", data, 16 + slot * 12)
                state[first + slot] = ref
                resource = resources.get(ref)
                buffer_offsets.append((number, opcode, first + slot, ref,
                                       offset, resource))
                if ref and (not resource or offset > resource[1]):
                    warnings.append("%d bad buffer bind stage=%#x slot=%d "
                                    "ref=%#x offset=%d resource=%r" %
                                    (number, opcode, first + slot, ref,
                                     offset, resource))
        elif opcode in (0x7e, 0x6f) and len(data) >= 20:
            slot, offset = struct.unpack_from("<IQ", data, 8)
            state = vertex_buffers if opcode == 0x7e else fragment_buffers
            ref = state.get(slot)
            resource = resources.get(ref)
            buffer_offsets.append((number, opcode, slot, ref,
                                   offset, resource))
            if ref is None or not resource or offset > resource[1]:
                warnings.append("%d bad buffer offset stage=%#x slot=%d "
                                "ref=%r offset=%d resource=%r" %
                                (number, opcode, slot, ref, offset, resource))
        elif opcode in (0x72, 0x81) and len(data) >= 16:
            first, count = struct.unpack_from("<II", data, 8)
            state = (fragment_textures if opcode == 0x72
                     else vertex_textures)
            if 16 + count * 4 > len(data):
                warnings.append("%d short texture bind" % number)
                continue
            for slot in range(count):
                ref = struct.unpack_from("<I", data, 16 + slot * 4)[0]
                state[first + slot] = ref
                if ref and not resources.get(ref):
                    warnings.append("%d missing texture bind %#x" %
                                    (number, ref))
                elif ref and resources[ref][4] == "0x7":
                    three_d_binds.append((number, opcode, first + slot, ref))
        elif opcode == 0x74 and len(data) >= 12:
            pipeline = struct.unpack_from("<I", data, 8)[0]
        elif opcode == 0x133 and len(data) >= 12:
            ref = struct.unpack_from("<I", data, 8)[0]
            mipmap_generations.append((number, ref, resources.get(ref)))
    print("indexed_draws=%d render_passes=%d opaque_sidecars=%d" %
          (len(indexed), len(render_passes), sidecars))
    print("buffer_operations=%d" % len(buffer_offsets))
    print("three_d_binds=%d three_d_draws=%d" %
          (len(three_d_binds), len(three_d_draws)))
    print("mipmap_generations=%d" % len(mipmap_generations))
    for item in mipmap_generations[:12]:
        print("mipmap_generation", item)
    for item in three_d_binds[:12]:
        print("three_d_bind", item)
    for item in three_d_draws[:12]:
        print("three_d_draw", item)
    for item in sorted(buffer_offsets, key=lambda x: x[4], reverse=True)[:12]:
        print("largest_buffer_offset", item)
    for item in indexed[-12:]:
        print("indexed", item)
    for item in render_passes[-12:]:
        print("render_pass", item)
    print("warnings=%d" % len(warnings))
    for item in warnings[:100]:
        print("warning", item)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit("usage: summarize-command-commit.py trace-dir sequence [archive.zip]")
    main(sys.argv[1], int(sys.argv[2]),
         sys.argv[3] if len(sys.argv) == 4 else None)
