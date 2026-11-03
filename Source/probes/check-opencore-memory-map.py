#!/usr/bin/env python3
"""Compile the actual vendored OcShrinkMemoryMap against the Snow panic fixture.

Default: reproduce the original bug, then test its one-block correction.
--source PATH: test the actual rebuilt/patched MemoryMap.c instead.
No VM, privileged operation, Apple binary or modified vendored archive needed.
"""
import argparse
from pathlib import Path
import subprocess
import tarfile
import tempfile


def function(source):
    begin = source.index('EFI_STATUS\nOcShrinkMemoryMap (')
    end = source.index('\nEFI_STATUS\nOcDeduplicateDescriptors (', begin)
    # Retain the source's original copyright/license notice in generated code.
    return source[:source.index('#include')] + source[begin:end]


def run(code, expected_bug, root):
    with tempfile.TemporaryDirectory(prefix='vim-oc-map-') as directory:
        path = Path(directory)
        implementation = path/'OcShrinkMemoryMap.c'
        implementation.write_text(function(code))
        executable = path/'fixture'
        command = ['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                   '-mmacosx-version-min=11.0',
                   '-DVIM_TEST_OPENCORE_FUNCTION="'+str(implementation)+'"']
        if expected_bug:
            command += ['-DVIM_EXPECT_UPSTREAM_BUG=1']
        subprocess.run(command+[str(root/'probes/efi-memory-map-normalization.c'),
                                '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.source:
        run(args.source.read_text(), False, root)
        return
    with tarfile.open(root/'vendor/opengl-opencore-source.tar.xz') as archive:
        source = archive.extractfile('OpenCorePkg/Library/OcMemoryLib/MemoryMap.c').read().decode()
    run(source, True, root)
    incorrect = '''  //
  // Handle last entries if they were merged.
  //
  if (HasEntriesToRemove) {
    *MemoryMapSize += DescriptorSize;
  }
'''
    assert source.count(incorrect) == 1
    run(source.replace(incorrect, ''), False, root)


if __name__ == '__main__':
    main()
