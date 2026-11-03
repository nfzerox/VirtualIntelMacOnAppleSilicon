#!/usr/bin/env python3
"""Diskless real-vCPU test of legacy GL transport BQL ownership.

A tiny real-mode ROM issues the actual device OUT instruction. A controlled
renderer withholds half a reply while QMP/qtest exercise the independent main
loop. No guest OS, GL driver, app control socket or real VM disk is involved.
"""
import argparse
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import tempfile
import threading
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('qemu', type=Path)
p.add_argument('bios_directory', type=Path)
p.add_argument('--accel', choices=['tcg', 'hvf'], default='tcg')
a = p.parse_args()


def exact(channel, size):
    result = bytearray()
    while len(result) < size:
        part = channel.recv(size-len(result))
        if not part:
            raise AssertionError('Unexpected controlled-renderer EOF')
        result.extend(part)
    return bytes(result)


def connected(path, child):
    channel = socket.socket(socket.AF_UNIX)
    deadline = time.monotonic()+5
    while True:
        try:
            channel.connect(str(path))
            channel.settimeout(5)
            return channel
        except (FileNotFoundError, ConnectionRefusedError):
            assert child.poll() is None and time.monotonic() < deadline
            time.sleep(.01)


def exercise(enabled, cancel=False, reset=False, scatter=None):
    print(json.dumps(dict(Begin=True, ReleaseBQL=enabled, Cancel=cancel, Reset=reset,
                          Scatter=scatter)), flush=True)
    with tempfile.TemporaryDirectory(prefix='vim-bql-fixture-') as scratch:
        base = Path(scratch)
        rom = bytearray(65536)
        # CLI; MOV DX,c000; MOV EAX,1000; OUT DX,EAX; INC DX;
        # XOR EAX,EAX; OUT DX,EAX; HLT; JMP HLT. No BIOS or OS services.
        code = bytes.fromhex('fa ba00c0 66b800100000 66ef 42 6631c0 66ef f4 ebfd')
        rom[:len(code)] = code
        rom[0xfff0:0xfff5] = bytes.fromhex('ea000000f0')
        (base/'fixture.rom').write_bytes(rom)
        listener = socket.socket(socket.AF_INET)
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(5)
        env = dict(os.environ)
        for key in ('VIM_INSTALLER_DISPLAY_SOCKET', 'VIM_OPENGL_VRAM_SERVICE',
                    'VIM_OPENGL_VRAM_ENDPOINT', 'VIM_OPENGL_RENDERER_PID'):
            env.pop(key, None)
        release, half_sent = threading.Event(), threading.Event()
        outcome = {}
        with (base/'qemu.log').open('wb') as log:
            child = subprocess.Popen([str(a.qemu.resolve()), '-L', str(a.bios_directory.resolve()),
                '-machine', 'q35', '-accel', a.accel, '-S', '-m', '128', '-nodefaults',
                '-bios', str(base/'fixture.rom'), '-vga', 'vmware', '-global',
                'vmware-svga.passthrough-opengl-bridge=on', '-global',
                f'vmware-svga.passthrough-opengl-port={listener.getsockname()[1]}',
                '-display', 'none', '-serial', 'none', '-monitor', 'none',
                '-qmp', f'unix:{base}/qmp,server=on,wait=off',
                '-qtest', f'unix:{base}/qtest,server=on,wait=off', '-qtest-log', '/dev/null'],
                stdout=log, stderr=log, env=env)
            try:
                qmp = connected(base/'qmp', child)
                pending = bytearray()

                def message(timeout=5):
                    deadline = time.monotonic()+timeout
                    while b'\n' not in pending:
                        remaining = deadline-time.monotonic()
                        if remaining <= 0 or not select.select([qmp], [], [], remaining)[0]:
                            raise TimeoutError('Controlled QMP observation interval ended')
                        part = qmp.recv(65536)
                        assert part, 'Unexpected QMP EOF'
                        pending.extend(part)
                    line, _, rest = pending.partition(b'\n')
                    pending[:] = rest
                    return json.loads(line)

                serial = 0

                def send(command, arguments=None):
                    nonlocal serial
                    serial += 1
                    qmp.sendall((json.dumps(dict(execute=command, arguments=arguments or {}, id=serial))+'\n').encode())
                    return serial

                def reply(identifier, timeout=5):
                    deadline = time.monotonic()+timeout
                    while True:
                        m = message(max(0, deadline-time.monotonic()))
                        if m.get('id') == identifier:
                            assert 'error' not in m, m
                            return m.get('return')

                def execute(command, arguments=None):
                    return reply(send(command, arguments))

                assert 'QMP' in message()
                execute('qmp_capabilities')
                matches = []
                for parent in ('/machine/peripheral-anon', '/machine/unattached', '/machine/peripheral'):
                    objects = execute('qom-list', dict(path=parent))
                    matches += [parent+'/'+row['name'] for row in objects if row['type'] == 'child<vmware-svga>']
                assert len(matches) == 1, matches
                path = matches[0]
                assert execute('qom-get', dict(path=path, property='x-passthrough-opengl-release-bql')) is True
                execute('qom-set', dict(path=path, property='x-passthrough-opengl-release-bql', value=enabled))
                control = connected(base/'qtest', child)
                q = control.makefile('rwb', buffering=0)

                def command(text):
                    q.write((text+'\n').encode())
                    words = q.readline().decode().strip().split()
                    assert words and words[0] == 'OK', words
                    return int(words[1], 0) if len(words) > 1 else None

                def config(offset, value):
                    command(f'outl 0xcf8 {pci | offset:#x}')
                    command(f'outl 0xcfc {value:#x}')

                def register(index, value):
                    command(f'outl 0xc000 {index:#x}')
                    command(f'outl 0xc001 {value:#x}')

                for slot in range(32):
                    pci = 0x80000000 | (slot << 11)
                    command(f'outl 0xcf8 {pci:#x}')
                    if command('inl 0xcfc') == 0x040515ad:
                        break
                else:
                    raise AssertionError('No research VMware device')
                vram = 0xf0000000
                config(0x10, 0xc001)
                config(0x14, vram)
                config(4, 3)
                panel_before = command('inl 0xc008')
                assert panel_before != 0xffffffff

                def packet(offset, op, token, size, receive=0, data=0):
                    for i, value in enumerate((0x56474c31, op, token, size, 0xffffffff, receive, data)):
                        command(f'writel {vram+offset+i*4:#x} {value:#x}')

                packet(0, 1, 0, 0)
                register(0x1000, 0)
                wire, _ = listener.accept()
                wire.settimeout(5)
                token = command(f'readl {vram+8:#x}')
                assert token and command(f'readl {vram+16:#x}') == 0
                simple = scatter in ('inline', 'direct')
                target = vram+24 if scatter == 'inline' else 0x200000 if scatter in ('ram', 'masked') else vram+0x50000
                if simple:
                    packet(0, 3 if scatter == 'inline' else 5, token, 4,
                           target-vram if scatter == 'direct' else 0)
                    command(f'writel {target:#x} 0xa5a5a5a5')
                    command(f'writel {target+4:#x} 0xa5a5a5a5')
                elif scatter:
                    ram = scatter != 'vram'
                    entries = [(target if ram else target-vram, 2, int(ram)),
                               (target+8 if ram else target-vram+8, 2, int(ram))]
                    if scatter == 'masked':
                        entries.insert(1, (0, 2, 2))
                    packet(0, 11 if scatter == 'masked' else 8 if ram else 7,
                           token, 2*len(entries), len(entries))
                    for i, (address, length, kind) in enumerate(entries):
                        at = vram+24+i*(16 if ram else 8)
                        command(f'writeq {at:#x} {address | (length << 32) if not ram else address:#x}')
                        if ram:
                            command(f'writel {at+8:#x} {length}')
                            command(f'writel {at+12:#x} {kind}')
                    for i in range(4):
                        command(f'writel {target+i*4:#x} 0xa5a5a5a5')
                else:
                    packet(0, 13, token, 4, 4, int.from_bytes(b'PING', 'little'))

                def renderer():
                    try:
                        outcome['request'] = 'READ' if scatter else exact(wire, 4).decode()
                        if not simple:
                            wire.sendall(b'PA')
                        half_sent.set()
                        assert release.wait(5)
                        if cancel and simple:
                            wire.shutdown(socket.SHUT_WR)
                        elif not cancel:
                            wire.sendall(b'PASS' if simple else b'XXSS' if scatter == 'masked' else b'SS')
                    except Exception as error:
                        outcome['error'] = str(error)

                worker = threading.Thread(target=renderer)
                worker.start()
                execute('cont')
                assert half_sent.wait(5), (base/'qemu.log').read_text()[-2000:]
                started = time.monotonic()
                status_id = send('query-status')
                try:
                    reply(status_id, .20)
                    responsive = True
                except TimeoutError:
                    responsive = False
                elapsed = time.monotonic()-started
                assert responsive == enabled, (enabled, responsive)
                reset_id = None
                if enabled:
                    # This is a distinct legitimate device access, not a
                    # recursive access from inside native renderer code.
                    assert command('inl 0xc008') == panel_before
                    # Small replies are atomic metadata. Scatter pixels are
                    # DMA and may contain a prefix, but no completion yet.
                    assert command(f'readl {vram+16:#x}') == 0xffffffff
                    if not scatter:
                        assert command(f'readl {vram+24:#x}') == int.from_bytes(b'PING', 'little')
                    if cancel:
                        packet(4096, 4, token, 0)
                        register(0x1000, 4096)
                    elif reset:
                        # Reset may wait for the active vCPU callback to
                        # finish. Request it now, then release the controlled
                        # peer; do not make their two completions depend on
                        # one another in the fixture itself.
                        reset_id = send('system_reset')
                    else:
                        # A competing request on the same stream is rejected,
                        # while a mutable receive-length cannot enlarge reply.
                        packet(4096, 13, token, 4, 4, 0xdeadbeef)
                        register(0x1000, 4096)
                        assert command(f'readl {vram+4096+16:#x}') == 0xffffffff
                        command(f'writel {vram+20:#x} 0xffffffff')
                        if scatter and not simple:
                            # The already validated vector, not a reread of
                            # this now-invalid guest address, owns the DMA.
                            command(f'writeq {vram+24:#x} 0xffffffffffffffff')
                release.set()
                worker.join(5)
                assert not worker.is_alive() and outcome == dict(request='READ' if scatter else 'PING'), outcome
                if not responsive:
                    reply(status_id)
                if reset_id is not None:
                    reply(reset_id)
                # Synchronize with the vCPU's callback return before checking
                # completion; a responsive main loop alone doesn't prove it.
                execute('stop')
                if not reset:
                    result = command(f'readl {vram+16:#x}')
                    if cancel:
                        assert result == 0xffffffff
                        if not scatter:
                            assert command(f'readl {vram+24:#x}') == int.from_bytes(b'PING', 'little')
                    else:
                        assert result == (6 if scatter == 'masked' else 4), result
                        if not scatter:
                            assert command(f'readl {vram+24:#x}') == int.from_bytes(b'PASS', 'little')
                        elif simple:
                            assert command(f'readl {target:#x}') == int.from_bytes(b'PASS', 'little')
                        else:
                            assert command(f'readw {target:#x}') == int.from_bytes(b'PA', 'little')
                            assert command(f'readw {target+8:#x}') == int.from_bytes(b'SS', 'little')
                    if simple:
                        assert command(f'readl {target+4:#x}') == 0xa5a5a5a5
                    elif scatter:
                        assert command(f'readw {target+2:#x}') == 0xa5a5
                        assert command(f'readl {target+4:#x}') == 0xa5a5a5a5
                        assert command(f'readw {target+10:#x}') == 0xa5a5
                assert 'Blocked re-entrant IO' not in (base/'qemu.log').read_text()
                return dict(ReleaseBQL=enabled, Cancel=cancel, Reset=reset, Scatter=scatter,
                            MainLoopResponsiveDuringRendererWait=responsive,
                            StatusObservationMilliseconds=round(elapsed*1000, 3), Passed=True)
            except Exception:
                print((base/'qemu.log').read_text()[-4000:])
                raise
            finally:
                release.set()
                if child.poll() is None:
                    # This is a disposable diskless ROM/qtest fixture, not
                    # an OS shutdown qualification. Do not leave a halted
                    # test CPU or blocked mock peer behind on any verdict.
                    child.kill()
                    child.wait()
                listener.close()


results = [exercise(False), exercise(True), exercise(True, cancel=True), exercise(True, reset=True)]
for scatter in ('vram', 'ram', 'masked', 'inline', 'direct'):
    results.extend([exercise(False, scatter=scatter), exercise(True, scatter=scatter),
                    exercise(True, cancel=True, scatter=scatter), exercise(True, reset=True, scatter=scatter)])
print(json.dumps(results, indent=2))
