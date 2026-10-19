#!/usr/bin/env python3
"""Shared release-input selection; new releases use XZ, old gzip stays readable."""
import gzip
import hashlib
import lzma
from pathlib import Path


def release_archive(directory):
    directory = Path(directory)
    found = [directory/name for name in ('guest-payload.tar.xz', 'guest-payload.tar.gz')
             if (directory/name).is_file()]
    if len(found) != 1:
        raise ValueError(f'Expected exactly one release archive in {directory}')
    return found[0]


def recompress(original, destination):
    """Change the compression envelope only; verify identical uncompressed bytes."""
    original, destination = Path(original), Path(destination)
    if original.suffix != '.gz' or destination.suffix != '.xz':
        raise ValueError('Expected gzip input and XZ output')
    before = hashlib.sha256()
    with gzip.open(original, 'rb') as source, destination.open('xb') as output:
        with lzma.LZMAFile(output, 'w', preset=6, check=lzma.CHECK_CRC64) as compressed:
            while block := source.read(1024*1024):
                before.update(block)
                compressed.write(block)
    after = hashlib.sha256()
    with lzma.open(destination, 'rb') as source:
        while block := source.read(1024*1024):
            after.update(block)
    if before.digest() != after.digest():
        raise ValueError('Recompression changed the uncompressed archive')
    return before.hexdigest()


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print('Verified identical tar SHA256:', recompress(args.input, args.output))
