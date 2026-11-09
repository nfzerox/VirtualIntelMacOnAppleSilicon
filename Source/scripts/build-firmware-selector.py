#!/usr/bin/env python3
"""Build the quiet x64 EFI profile selector with pinned firmware tools."""
import argparse
from pathlib import Path
import runpy
import struct
import subprocess
import tempfile


def build(output, tools=None, headers=None):
    source = Path(__file__).resolve().parents[1]
    tools = Path(tools or source/'Build/FirmwareTools')
    builder = runpy.run_path(str(source/'scripts/build-guest-firmware.py'))
    llvm, _, _ = builder['prepare_tools'](tools.resolve(), 4, True)
    if headers is None:
        root = source/'Build/BootPolicy/edk2'
        if not (root/'MdePkg/Include/Uefi.h').is_file():
            from firmware_source import extract
            root = source/'Build/FirmwareSelector/edk2'
            root.parent.mkdir(parents=True, exist_ok=True)
            extract(source, root)
        headers = root/'MdePkg/Include'
    headers = Path(headers)
    output = Path(output)
    if output.exists():
        raise ValueError('Use a new selector output path')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vim-efi-selector-') as temporary:
        obj = Path(temporary)/'Selector.obj'
        subprocess.run([str(llvm/'bin/clang'), '--target=x86_64-w64-windows-gnu',
                        '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                        '-fshort-wchar', '-mno-red-zone', '-Os', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(headers), '-I'+str(headers/'X64'),
                        '-c', str(source/'compat/FirmwareSelector/FirmwareSelector.c'), '-o', str(obj)], check=True)
        subprocess.run([str(llvm/'bin/lld-link'), '/subsystem:efi_application',
                        '/entry:EfiMain', '/nodefaultlib', '/machine:x64',
                        '/out:'+str(output), str(obj)], check=True)
    data = output.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if (data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x8664
            or struct.unpack_from('<H', data, pe+24)[0] != 0x20b
            or struct.unpack_from('<H', data, pe+24+68)[0] != 10
            or any(struct.unpack_from('<II', data, pe+24+112+8))):
        raise ValueError('Selector must be an x64 EFI application without imported libraries')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tools', type=Path)
    parser.add_argument('--headers', type=Path)
    args = parser.parse_args()
    print(build(args.output, args.tools, args.headers))
