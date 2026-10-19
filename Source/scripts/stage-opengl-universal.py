#!/usr/bin/env python
"""Merge guest-native non-x86_64 GL slices into a staged Catalina framework.

Python 2.7 compatible; no Xcode needed in guest. Never writes guest sources.
Caller signs modified binaries and the framework afterwards.
"""
from __future__ import print_function
import runpy
import os
import struct
import sys
import subprocess
import tempfile
from pvg_macho import require, thin

native32 = runpy.run_path(os.path.join(os.path.dirname(__file__), 'stage-opengl-native32.py'))


def slices(data):
    if data[:4] not in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
        require(data[:4] in (b'\xce\xfa\xed\xfe', b'\xcf\xfa\xed\xfe'), 'not Mach-O')
        cpu, subtype = struct.unpack_from('<II', data, 4)
        return [(cpu, subtype, 12, data)]
    wide = data[:4] == b'\xca\xfe\xba\xbf'
    count = struct.unpack_from('>I', data, 4)[0]
    stride = 32 if wide else 20
    require(count <= 64 and 8+count*stride <= len(data), 'invalid fat header')
    result = []
    for index in range(count):
        pos = 8+index*stride
        cpu, subtype = struct.unpack_from('>II', data, pos)
        offset, size = struct.unpack_from('>QQ' if wide else '>II', data, pos+8)
        alignment = struct.unpack_from('>I', data, pos+(24 if wide else 16))[0]
        require(offset+size <= len(data) and alignment <= 20, 'invalid fat slice')
        result.append((cpu, subtype, alignment, data[offset:offset+size]))
    require(len(set(s[0] for s in result)) == len(result), 'duplicate architectures')
    return result


def combine(parts):
    cursor = 8+20*len(parts)
    records, chunks = [], []
    for cpu, subtype, alignment, content in parts:
        aligned = (cursor+(1 << alignment)-1) & ~((1 << alignment)-1)
        records.append(struct.pack('>IIIII', cpu, subtype, aligned, len(content), alignment))
        chunks.append(b'\0'*(aligned-cursor)+content)
        cursor = aligned+len(content)
    return struct.pack('>II', 0xcafebabe, len(parts))+b''.join(records)+b''.join(chunks)


def native_entry(path, tool):
    # The native tool only creates new files; use an owned staging directory.
    directory = tempfile.mkdtemp(prefix='pvg-gl32-')
    output = os.path.join(directory, 'native32')
    try:
        process = subprocess.Popen([tool, 'opengl-native32', path, output],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        unused, error = process.communicate()
        if process.returncode:
            message = error.decode('utf-8', 'replace').strip()
            if message == 'Routing facade is not a native backup':
                message = 'routing facade is not a native backup'
            raise ValueError(message)
        with open(output, 'rb') as f:
            return f.read()
    finally:
        if os.path.exists(output):
            os.unlink(output)
        os.rmdir(directory)


def native_merge(source, donor, tool):
    directory = tempfile.mkdtemp(prefix='pvg-gl-merge-')
    paths = [os.path.join(directory, n) for n in ('native', 'donor', 'merged')]
    try:
        for path, data in zip(paths, (source, donor)):
            with open(path, 'wb') as f:
                f.write(data)
        subprocess.check_call([tool, 'merge-x86_64']+paths)
        with open(paths[2], 'rb') as f:
            return f.read()
    finally:
        for path in paths:
            if os.path.exists(path):
                os.unlink(path)
        os.rmdir(directory)


def merge(guest, staged, backups=(), native_tool=None):
    changed = []
    manifest = os.path.join(staged, 'Versions/A/Resources/PVGDonorFiles.txt')
    with open(manifest) as f:
        donor_files = set(line.rstrip('\n') for line in f)
    for directory, dirs, files in os.walk(staged):
        for name in files:
            path = os.path.join(directory, name)
            if os.path.islink(path):
                continue
            with open(path, 'rb') as f:
                donor = f.read()
            if donor[:4] not in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
                continue
            relative = os.path.relpath(path, staged)
            original = os.path.join(guest, relative)
            if not os.path.isfile(original):
                continue  # Catalina-only component; no native slice to lose.
            with open(original, 'rb') as f:
                source = f.read()
            if relative not in donor_files:
                # Older CPU compiler plugins/tools not supplied by Catalina
                # stay entirely native, including their current x86_64 slice.
                with open(path, 'wb') as f:
                    f.write(source)
                changed.append(path)
                continue
            parts = [part for part in slices(source) if part[0] != 0x1000007]
            if relative == 'Versions/A/OpenGL':
                try:
                    content = (native_entry(original, native_tool) if native_tool else
                               native32['prepare'](source, native32['STOCK']))
                except ValueError as error:
                    if str(error) != 'routing facade is not a native backup':
                        raise
                    candidates = [p for p in backups if os.path.isfile(p)]
                    require(candidates, 'old facade requires preserved native OpenGL backup')
                    if native_tool:
                        content = native_entry(candidates[0], native_tool)
                    else:
                        with open(candidates[0], 'rb') as f:
                            content = native32['prepare'](f.read(), native32['STOCK'])
                parts = [(7, struct.unpack_from('<I', content, 8)[0], 12, content)]
                source = content
            if not parts:
                continue
            if native_tool:
                combined = native_merge(source, donor, native_tool)
            else:
                replacement = thin(donor)
                parts.append((0x1000007, struct.unpack_from('<I', replacement, 8)[0], 12, replacement))
                combined = combine(parts)
            with open(path, 'wb') as f:
                f.write(combined)
            changed.append(path)
    require(any(p.endswith('/Versions/A/OpenGL') for p in changed), 'native i386 entry not preserved')
    return changed


if __name__ == '__main__':
    tool = None
    if len(sys.argv) > 2 and sys.argv[1] == '--native-tool':
        tool = sys.argv[2]
        del sys.argv[1:3]
    require(len(sys.argv) >= 3, 'usage: stage-opengl-universal.py GUEST_FRAMEWORK STAGED_FRAMEWORK [OLD_NATIVE_BACKUPS...]')
    for path in merge(sys.argv[1], sys.argv[2], sys.argv[3:], native_tool=tool):
        print(path)
