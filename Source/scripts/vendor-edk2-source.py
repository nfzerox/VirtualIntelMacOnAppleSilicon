#!/usr/bin/env python3
"""Export pristine pinned firmware sources, including required submodule content.

Maintenance only. Never fetches, modifies inputs, or includes .git/user/build
state. Normal firmware rebuilds use this tracked archive and need no checkouts.
"""
import argparse
import copy
import gzip
import hashlib
import io
from pathlib import Path
import plistlib
import posixpath
import subprocess
import tarfile

from firmware_source import SUBMODULES, checkout_receipt


def append_tree(output, root, omissions, prefix=''):
    process = subprocess.Popen(['git', '-C', str(root), 'archive', '--format=tar', 'HEAD'],
                               stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=process.stdout, mode='r|') as stream:
            for original in stream:
                member = copy.copy(original)
                member.name = prefix+member.name
                if member.issym():
                    target = posixpath.normpath(posixpath.join(
                        posixpath.dirname(member.name), member.linkname))
                    if member.linkname.startswith('/') or target == '..' or target.startswith('../'):
                        # The Unix emulator's X11 include hack points outside
                        # the source tree. It is not an x64 OVMF build input;
                        # retain its description, never materialize host links.
                        if member.name != 'EmulatorPkg/Unix/Host/X11IncludeHack':
                            raise RuntimeError('Unqualified external source symlink: '+member.name)
                        omissions[member.name] = member.linkname
                        continue
                if member.islnk():
                    member.linkname = prefix+member.linkname
                if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                    raise RuntimeError('Unsupported source member: '+member.name)
                member.uid = member.gid = member.mtime = 0
                member.uname = member.gname = ''
                member.pax_headers = {}
                member.mode &= 0o777
                output.addfile(member, stream.extractfile(original) if member.isfile() else None)
    finally:
        process.stdout.close()
        if process.wait():
            raise RuntimeError('git archive failed: '+str(root))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = args.source.resolve()
    receipt = checkout_receipt(root)
    omissions = {}
    manifest = args.output.with_suffix('').with_suffix('.sha256')
    if args.output.exists() or manifest.exists():
        parser.error('Use a new output and checksum manifest; never overwrite source inputs.')
    with args.output.open('xb') as file, gzip.GzipFile(filename='', mode='wb',
            fileobj=file, mtime=0, compresslevel=6) as zipped:
        with tarfile.open(fileobj=zipped, mode='w|', format=tarfile.PAX_FORMAT) as output:
            append_tree(output, root, omissions)
            for relative in sorted(SUBMODULES):
                append_tree(output, root/relative, omissions, relative+'/')
            data = plistlib.dumps({**receipt, 'OmittedNonBuildSymlinks': omissions}, sort_keys=True)
            member = tarfile.TarInfo('VIMSourceReceipt.plist')
            member.size, member.mode = len(data), 0o644
            output.addfile(member, io.BytesIO(data))
    if checkout_receipt(root) != receipt:
        raise RuntimeError('Source changed during export; discard this unqualified archive.')
    with args.output.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    manifest.write_text(digest+'  '+args.output.name+'\n')
    print(digest, args.output.name)


if __name__ == '__main__':
    main()
