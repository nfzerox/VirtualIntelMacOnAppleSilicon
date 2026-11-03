#!/usr/bin/env python3
"""Inspect actual generated Q35 AML in two blank, stopped laboratory machines.

Never opens a VM disk or connects to the research app's control socket. Read
fw_cfg using QEMU's qtest DMA interface, then disassemble the actual DSDT with
an explicitly supplied iasl. This qualifies generation/scope, NOT guest boot,
ACPI method execution, native rendering or desktop performance.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile


def extract(qemu, bios, enabled):
    with tempfile.TemporaryDirectory(prefix='vim-pirq-aml-') as temporary:
        endpoint = Path(temporary) / 'qtest'
        command = [str(qemu), '-machine', 'q35', '-accel', 'hvf',
            '-m', '32M', '-nodefaults', '-display', 'none', '-S', '-L', str(bios),
            '-qtest', 'unix:' + str(endpoint) + ',server=on,wait=off',
            '-qmp', 'stdio']
        if enabled:
            command += ['-global', 'ICH9-LPC.x-apic-pirq-f-native-only=on']
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True)
        try:
            if 'QMP' not in json.loads(process.stdout.readline()):
                raise RuntimeError('Missing own blank-machine QMP greeting')
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(10)
                connection.connect(str(endpoint))
                stream = connection.makefile('rwb', buffering=0)

                def request(command):
                    stream.write((command + '\n').encode())
                    while True:
                        line = stream.readline().decode().strip()
                        if line.startswith('OK'):
                            return line[2:].strip()
                        if not line.startswith('IRQ '):
                            raise RuntimeError('Own qtest failed: ' + line)

                def read(key, length):
                    if not 0 < length <= 4 * 1024 * 1024:
                        raise RuntimeError('Unreasonable fw_cfg length')
                    descriptor = struct.pack('>IIQ', (key << 16) | 0x0a, length, 0x2000)
                    request('write 0x1000 0x10 0x' + descriptor.hex())
                    request('outl 0x514 0')
                    request('outl 0x518 0x00100000')
                    status = bytes.fromhex(request('read 0x1000 0x4')[2:])
                    if status != b'\0' * 4:
                        raise RuntimeError('fw_cfg DMA did not complete cleanly')
                    return bytes.fromhex(request('read 0x2000 ' + hex(length))[2:])

                count, = struct.unpack('>I', read(0x19, 4))
                if count > 1000:
                    raise RuntimeError('Unreasonable fw_cfg directory')
                directory = read(0x19, 4 + count * 64)
                matches = []
                for i in range(count):
                    size, key, reserved, name = struct.unpack_from('>IHH56s', directory, 4 + i * 64)
                    if name.rstrip(b'\0') == b'etc/acpi/tables':
                        matches.append(read(key, size))
                if len(matches) != 1:
                    raise RuntimeError('Expected one actual ACPI table blob')
                blob = matches[0]
                at = blob.find(b'DSDT')
                if at < 0 or blob.find(b'DSDT', at + 4) >= 0:
                    raise RuntimeError('Expected one DSDT')
                length, = struct.unpack_from('<I', blob, at + 4)
                table = bytearray(blob[at:at + length])
                if len(table) != length or length < 36:
                    raise RuntimeError('Invalid actual DSDT length')
                # BIOS linker normally fills this checksum. Only this isolated
                # disassembly copy is finalized; no guest or firmware is edited.
                table[9] = 0
                table[9] = (-sum(table)) & 255
                return table
        finally:
            # Only this diskless self-test process is owned by the harness.
            if process.poll() is None:
                process.communicate('{"execute":"qmp_capabilities"}\n{"execute":"quit"}\n', timeout=10)
            if process.returncode:
                raise RuntimeError('Own blank machine did not quit cleanly')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu', type=Path, required=True)
    p.add_argument('--iasl', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    qemu, iasl = a.qemu.resolve(strict=True), a.iasl.resolve(strict=True)
    bios = Path(__file__).resolve().parents[1] / 'vendor/qemu/pc-bios'
    a.output.mkdir(parents=True, exist_ok=False)
    for enabled in (False, True):
        table = a.output / ('native-only' if enabled else 'stock')
        aml = table.with_suffix('.aml')
        aml.write_bytes(extract(qemu, bios, enabled))
        result = subprocess.run([str(iasl), '-d', str(aml.resolve())], text=True,
                                capture_output=True, timeout=15)
        table.with_suffix('.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Actual DSDT disassembly failed')
        text = table.with_suffix('.dsl').read_text()
        if ('Name (VPFS, Zero)' in text) != enabled or ('Name (VPFV, Zero)' in text) != enabled:
            raise RuntimeError('Policy leaked into stock AML or is absent from candidate')
        if enabled:
            for fragment in ('Method (_PIC, 1, Serialized)', '\\_SB.PRQF |= 0x80',
                             'VPFS = \\_SB.PRQF', '\\_SB.PRQF = VPFS'):
                if fragment not in text:
                    raise RuntimeError('Missing actual policy operation: ' + fragment)
        print('PASS actual Q35 AML', 'native-only F' if enabled else 'stock/unchanged scope')


if __name__ == '__main__':
    main()
