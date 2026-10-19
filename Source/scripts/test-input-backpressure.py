#!/usr/bin/env python3
"""Exercise real QMP/HID capacity without a guest disk or GUI."""
import argparse
import json
import os
import pathlib
import signal
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('qemu')
args = p.parse_args()
def timeout(signum, frame):
    raise TimeoutError('QMP keyboard test exceeded 30 seconds')
signal.signal(signal.SIGALRM, timeout)

def run(enabled):
    with tempfile.TemporaryDirectory(prefix='vim-keyboard-') as directory:
        bios = pathlib.Path(directory) / 'halt.bin'
        # Reset-vector tight loop: no firmware USB polling can drain the queue.
        image = bytearray(65536)
        image[-16:-14] = b'\xeb\xfe'
        bios.write_bytes(image)
        env = dict(os.environ, VIM_INPUT_BACKPRESSURE='1' if enabled else '0')
        proc = subprocess.Popen([args.qemu, '-machine', 'q35', '-accel', 'tcg',
            '-m', '64M', '-nodefaults', '-display', 'none', '-bios', str(bios),
            '-device', 'qemu-xhci', '-device', 'usb-kbd', '-qmp', 'stdio'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=env)
        try:
            signal.alarm(30)
            assert 'QMP' in json.loads(proc.stdout.readline())
            sequence = 0
            def request(command, arguments=None):
                nonlocal sequence
                sequence += 1
                proc.stdin.write(json.dumps(dict(execute=command,
                    arguments=arguments or {}, id=sequence))+'\n')
                proc.stdin.flush()
                while True:
                    line = proc.stdout.readline()
                    assert line, proc.stderr.read()
                    reply = json.loads(line)
                    if reply.get('id') == sequence:
                        return reply
            assert 'return' in request('qmp_capabilities')
            def keys(n):
                return request('input-send-event', {'events': [
                    {'type': 'key', 'data': {'down': bool(i % 2 == 0),
                     'key': {'type': 'qcode', 'data': 'a'}}} for i in range(n)]})
            if enabled:
                # Oversized rejected batch must not fill the queue partially.
                assert keys(1024)['error']['desc'] == 'VIMInputQueueFull'
                assert 'return' in keys(1)
                accepted = 1
                for _ in range(1024):
                    result = keys(1)
                    if 'error' in result:
                        assert result['error']['desc'] == 'VIMInputQueueFull'
                        break
                    accepted += 1
                else:
                    raise AssertionError('HID queue never filled')
                assert keys(1)['error']['desc'] == 'VIMInputQueueFull'
                print(f'Backpressure: atomic rejection and {accepted}-entry capacity verified')
            else:
                assert 'return' in keys(1024)
                print('Legacy QMP behavior unchanged without opt-in')
            request('quit')
            proc.wait(timeout=10)
        finally:
            signal.alarm(0)
            if proc.poll() is None:
                proc.kill()
                proc.wait()

run(True)
run(False)
