#!/usr/bin/env python3
"""Bounded, diskless local xHCI subword-MMIO check; never contacts a real VM.

The macOS driver reads HCIVERSION with a 16-bit load at capability offset 2.
Compare byte/word reads against the same controller's aligned dword value.
"""
import argparse
from pathlib import Path
import selectors
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('qemu', type=Path)
    parser.add_argument('--model', choices=('qemu-xhci', 'nec-usb-xhci'), default='qemu-xhci')
    parser.add_argument('--bios-directory', type=Path, default=Path(__file__).resolve().parents[1]/'vendor/qemu/pc-bios')
    args = parser.parse_args()
    process = subprocess.Popen([str(args.qemu.resolve()), '-L', str(args.bios_directory),
        '-machine', 'q35', '-accel', 'tcg', '-S', '-nodefaults', '-display', 'none',
        '-device', args.model+',addr=0x7', '-qtest', 'stdio', '-qtest-log', '/dev/null'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    def command(text):
        process.stdin.write((text+'\n').encode())
        response = bytearray()
        while not response.endswith(b'\n'):
            if not selector.select(5):
                raise RuntimeError('qtest reply timed out: '+text)
            byte = process.stdout.read(1)
            if not byte:
                raise RuntimeError('qtest exited: '+process.stderr.read(8192).decode(errors='replace'))
            response += byte
        fields = response.decode().split()
        if not fields or fields[0] != 'OK':
            raise RuntimeError(response.decode())
        return int(fields[1], 0) if len(fields)>1 else None

    try:
        # Map only this disposable controller's BAR; enable PCI memory decoding.
        base = 0xe0000000
        for offset, value in ((0x10, base), (0x14, 0), (0x04, 2)):
            command('outl 0xcf8 '+hex(0x80003800+offset))
            command(('outw' if offset == 4 else 'outl')+' 0xcfc '+hex(value))
        word = command('readl '+hex(base))
        assert word == 0x01000040, hex(word)
        for width, opcode in ((2, 'readw'), (1, 'readb')):
            for offset in range(0, 4, width):
                actual = command(opcode+' '+hex(base+offset))
                expected = (word >> (offset*8)) & ((1 << (width*8))-1)
                print(f'{args.model}: {width*8}-bit +{offset}: {actual:#x}, expected {expected:#x}', flush=True)
                assert actual == expected
    finally:
        selector.close()
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


if __name__ == '__main__':
    main()
