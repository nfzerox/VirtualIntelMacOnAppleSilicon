#!/usr/bin/env python3
"""Diskless local controller check: stream-array pointer and dequeue cycle.

Uses only a disposable QEMU and synthetic RAM/USB device, never a real VM or USB
drive. xHCI 1.2b sections 3.2.4/4.12: a streaming endpoint's dequeue field is
the stream-array address, not a per-stream transfer-ring address.
"""
import argparse
from pathlib import Path
import selectors
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('qemu', type=Path)
    parser.add_argument('--case', choices=('array', 'cycle', 'both'), default='both')
    args = parser.parse_args()
    bios = Path(__file__).resolve().parents[1]/'vendor/qemu/pc-bios'
    process = subprocess.Popen([str(args.qemu.resolve()), '-L', str(bios),
        '-machine', 'q35', '-accel', 'tcg', '-S', '-nodefaults', '-display', 'none',
        '-device', 'qemu-xhci,id=xhci,addr=0x7', '-device', 'usb-uas,bus=xhci.0',
        '-qtest', 'stdio', '-qtest-log', '/dev/null'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    def q(text):
        process.stdin.write((text+'\n').encode())
        response = bytearray()
        while not response.endswith(b'\n'):
            if not selector.select(5):
                raise RuntimeError('qtest reply timed out: '+text)
            byte = process.stdout.read(1)
            if not byte:
                raise RuntimeError('qtest ended: '+process.stderr.read(8192).decode(errors='replace'))
            response += byte
        fields = response.decode().split()
        if not fields or fields[0] != 'OK':
            raise RuntimeError(response.decode())
        return int(fields[1], 0) if len(fields) > 1 else None

    def read(address):
        return q(f'readl {address:#x}')

    def write(address, value):
        q(f'writel {address:#x} {value:#x}')

    def write64(address, value):
        write(address, value & 0xffffffff)
        write(address+4, value >> 32)

    base = 0xe0000000
    dcbaa, erst, commands, events, input_ctx, output_ctx, ep0, array, ring = range(0x100000, 0x109000, 0x1000)
    command_index = event_index = 0

    def command(kind, parameter=0, slot=0):
        nonlocal command_index, event_index
        address = commands+16*command_index
        write64(address, parameter)
        write(address+12, (kind << 10) | (slot << 24) | 1)
        command_index += 1
        write(doorbell, 0)
        while event_index < 64:
            address = events+16*event_index
            control = read(address+12)
            assert control & 1, f'missing completion for command {kind}'
            status = read(address+8)
            event_index += 1
            write64(runtime+0x38, events+16*event_index | 8)
            if (control >> 10) & 0x3f != 33:
                continue
            assert status >> 24 == 1, f'command {kind} completion {status >> 24}'
            return control >> 24
        raise RuntimeError('event ring exhausted')

    try:
        for offset, value in ((0x10, base), (0x14, 0), (0x04, 6)):
            q('outl 0xcf8 '+hex(0x80003800+offset))
            q(('outw' if offset == 4 else 'outl')+' 0xcfc '+hex(value))
        oper = base+(read(base) & 0xff)
        runtime = base+(read(base+0x18) & ~0x1f)
        doorbell = base+(read(base+0x14) & ~3)
        write(oper, 2)
        q('memset 0x100000 0x10000 0')
        write(oper+0x38, 1)
        write64(oper+0x30, dcbaa)
        write64(erst, events)
        write(erst+8, 64)
        write(runtime+0x28, 1)
        write64(runtime+0x30, erst)
        write64(runtime+0x38, events | 8)
        write64(oper+0x18, commands | 1)
        write(oper, 1)
        ports = read(base+4) >> 24
        port = next(i for i in range(ports) if read(oper+0x400+16*i) & 1)
        write(oper+0x400+16*port, (1 << 9) | (1 << 4))
        slot = command(9)
        write64(dcbaa+8*slot, output_ctx)
        write(input_ctx+4, 3)
        write(input_ctx+0x20, 1 << 27)
        write(input_ctx+0x24, (port+1) << 16)
        write(input_ctx+0x44, (512 << 16) | (4 << 3))
        write64(input_ctx+0x48, ep0 | 1)
        command(11, input_ctx, slot)
        q(f'memset {input_ctx:#x} 0x1000 0')
        write(input_ctx+4, 9)  # add slot and bulk-IN endpoint 1 (DCI 3)
        write(input_ctx+0x20, 3 << 27)
        write(input_ctx+0x24, (port+1) << 16)
        write(input_ctx+0x80, (1 << 15) | (1 << 10))  # linear, four contexts
        write(input_ctx+0x84, (1024 << 16) | (6 << 3))
        write64(input_ctx+0x88, array)
        command(12, input_ctx, slot)
        cycle = 0 if args.case == 'cycle' else 1
        write64(array+16, ring | 2 | cycle)
        # The first TRB is not owned by the controller. Kicking must leave its
        # dequeue cycle untouched, regardless of its initial value.
        write(ring+12, (1 << 10) | (1-cycle))
        write(doorbell+4*slot, 3 | (1 << 16))
        observed_array = read(output_ctx+0x68) | (read(output_ctx+0x6c) << 32)
        observed_cycle = read(array+16) & 1
        if args.case != 'cycle':
            assert observed_array == array, f'array pointer overwritten: {observed_array:#x} != {array:#x}'
        assert observed_cycle == cycle, f'dequeue cycle overwritten: {observed_cycle} != {cycle}'
        if args.case == 'both':
            write64(array+32, ring+0x100 | 2)  # second stream, initial DCS 0
            write(ring+0x100+12, (1 << 10) | 1)
            write(doorbell+4*slot, 3 | (2 << 16))
            assert read(array+32) & 1 == 0, 'initial DCS 0 became 1'
        print(f'PASS {args.case}: stream-array pointer and initial dequeue cycle preserved')
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
