#!/usr/bin/env python3
"""Check completed-frame ownership in an isolated, diskless QEMU device.

No guest OS, real VM monitor connection, GL renderer or pixel timing heuristic.
Write/publish one framebuffer, overwrite guest VRAM WITHOUT publishing, then
publish again. Only the last completed UPDATE may appear in each screendump.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--bios-dir', type=Path, required=True)
    parser.add_argument('--boot-handoff', action='store_true', help='Exercise the first VBE-to-SVGA pitch change')
    parser.add_argument('--boot-handoff-delay', type=float, default=0,
                        help='Also check an unchanged boot image after this many real seconds (requires --boot-handoff)')
    parser.add_argument('--boot-framebuffer-read', action='store_true',
                        help='Verify the accelerated bootstrap reads the completed fitted frame')
    parser.add_argument('--boot-unpublished-write', action='store_true',
                        help='Modify old-layout VRAM without UPDATE before the delayed handoff check')
    args = parser.parse_args()
    if not 0 <= args.boot_handoff_delay <= 30 or (args.boot_handoff_delay and not args.boot_handoff):
        parser.error('--boot-handoff-delay requires --boot-handoff and 0..30 seconds')
    if args.boot_framebuffer_read and not args.boot_handoff:
        parser.error('--boot-framebuffer-read requires --boot-handoff')
    if args.boot_unpublished_write and (not args.boot_handoff or args.boot_handoff_delay < 3.2):
        parser.error('--boot-unpublished-write requires --boot-handoff and a delay of at least3.2 seconds')
    with tempfile.TemporaryDirectory(prefix='vim-scanout-') as scratch:
        work = Path(scratch)
        environment = dict(os.environ, VIM_DISPLAY_EVENT_DRIVEN='1')
        # A stopped CPU is intentional. Only this test's QTest device accesses
        # run; it cannot touch an installed VM, guest disk or host USB device.
        command = [str(args.qemu.resolve()), '-L', str(args.bios_dir.resolve()),
                   '-machine', 'q35', '-accel', 'tcg', '-S', '-nodefaults',
                   '-display', 'none', '-vga', 'none', '-device',
                   'vmware-svga,id=svga,bus=pcie.0,addr=0x4,passthrough-opengl-bridge=on',
                   '-qtest', f'unix:{work}/test,server=on,wait=off',
                   '-qmp', f'unix:{work}/monitor,server=on,wait=off']
        with (work/'stderr').open('wb') as log:
            process = subprocess.Popen(command, env=environment, stdout=subprocess.DEVNULL, stderr=log)
            connections = []
            try:
                def connect(name):
                    connection = socket.socket(socket.AF_UNIX)
                    connection.settimeout(10)
                    deadline = time.monotonic()+10
                    while True:
                        try:
                            connection.connect(str(work/name))
                            break
                        except (FileNotFoundError, ConnectionRefusedError):
                            if process.poll() is not None or time.monotonic() >= deadline:
                                raise RuntimeError((work/'stderr').read_text())
                            time.sleep(0.02)
                    connections.append(connection)
                    return connection, connection.makefile('rb')
                qmp, monitor = connect('monitor')
                assert 'QMP' in json.loads(monitor.readline())
                def execute(name, **arguments):
                    qmp.sendall(json.dumps({'execute': name, 'arguments': arguments}).encode()+b'\n')
                    while True:
                        reply = json.loads(monitor.readline())
                        if 'event' not in reply:
                            assert 'return' in reply, reply
                            return reply['return']
                execute('qmp_capabilities')
                device, test = connect('test')
                def access(command):
                    device.sendall(command.encode()+b'\n')
                    reply = test.readline().decode().strip()
                    assert reply.startswith('OK'), reply
                    return reply
                def out(port, value):
                    access(f'outl {port:#x} {value:#x}')
                def pci(register, value):
                    out(0xcf8, 0x80002000+register)
                    out(0xcfc, value)
                pci(0x10, 0xc001)
                pci(0x14, 0xd0000000)
                pci(0x18, 0xe0000000)
                pci(4, 3)
                def register(index, value):
                    out(0xc000, index)
                    out(0xc001, value)
                def write(address, data):
                    access(f'write {address:#x} {len(data):#x} 0x{data.hex()}')
                def frame(name):
                    path = work/(name+'.ppm')
                    execute('screendump', filename=str(path))
                    return path.read_bytes()
                if args.boot_handoff:
                    def vbe(index, value):
                        access(f'outw 0x1ce {index:#x}')
                        access(f'outw 0x1cf {value:#x}')
                    access('outb 0x3c2 1')
                    access('inb 0x3da')
                    access('outb 0x3c0 0x20')
                    for index, value in ((0, 0xb0c5), (4, 0), (1, 320), (2, 200), (3, 32), (4, 0x41)):
                        vbe(index, value)
                    # Every row differs: a wrong source pitch cannot masquerade
                    # as a valid uniform gray boot background.
                    boot = b''.join(struct.pack('<I', 0xff000000 | (y << 8) | x)
                                    for y in range(200) for x in range(320))
                    write(0xd0000000, boot)
                    before = frame('vbe-boot')
                    assert before.startswith(b'P6\n320 200\n255\n'), before[:100]
                    for index, value in ((0, 0x90000002), (2, 512), (3, 300), (7, 32), (1, 1)):
                        register(index, value)
                    write(0xe0000000, struct.pack('<4I', 16, 0x10000, 16, 16))
                    register(20, 1)
                    write(0xe0000010, struct.pack('<5I', 1, 0, 0, 512, 300))
                    access('writel 0xe0000008 36')
                    register(21, 1)
                    frame('svga-seeded')
                    # The initial invalidation publishes the seeded image.
                    # A subsequent UPDATE before WindowServer repaints must
                    # not reinterpret the unchanged boot bytes at a new pitch.
                    write(0xe0000008, struct.pack('<2I', 16, 16))
                    write(0xe0000010, struct.pack('<5I', 1, 0, 0, 512, 300))
                    access('writel 0xe0000008 36')
                    register(21, 1)
                    after = frame('svga-before-repaint')
                    assert after.startswith(b'P6\n512 300\n255\n'), after[:100]
                    old = before.split(b'\n', 3)[3]
                    new = after.split(b'\n', 3)[3]
                    # Preserve aspect and optical placement, not a small
                    # top-left copy. 320x200 fits as480x300 with16px side bars.
                    def fitted(pixels, width, height):
                        scaled_width, scaled_height = width, 200*width//320
                        if scaled_height > height:
                            scaled_width, scaled_height = 320*height//200, height
                        left, top = (width-scaled_width)//2, (height-scaled_height)//2
                        for y in range(height):
                            for x in range(width):
                                value = pixels[(y*width+x)*3:(y*width+x+1)*3]
                                if left <= x < left+scaled_width and top <= y < top+scaled_height:
                                    # Pixman's16.16 transform samples pixel
                                    # centers; use its actual fixed precision.
                                    sx = min(319, ((2*(x-left)+1)*(320*65536//scaled_width)) >> 17)
                                    sy = min(199, ((2*(y-top)+1)*(200*65536//scaled_height)) >> 17)
                                    expected = old[(sy*320+sx)*3:(sy*320+sx+1)*3]
                                else:
                                    expected = b'\0\0\0'
                                assert value == expected, f'Boot fit mismatch at {x},{y}: {value.hex()} != {expected.hex()}'
                    fitted(new,512,300)
                    print('PASS: first VBE-to-SVGA handoff fits and centers the completed boot image')
                    if args.boot_framebuffer_read:
                        def read_register(index):
                            out(0xc000,index)
                            return int(access('inl 0xc001').split()[-1],16)
                        assert read_register(0x1019)==0x56424631, 'Missing completed boot-frame read capability'
                        def read(address,length):
                            return bytes.fromhex(access(f'read {address:#x} {length:#x}').split()[-1][2:])
                        command=0x200000
                        def boot_read(entries):
                            total=sum(e[2] for e in entries)
                            write(0xd0000000+command,struct.pack('<6I',0x56474c31,16,0,total,0xffffffff,len(entries))+
                                  b''.join(struct.pack('<Q4I',destination,source,n,kind,0) for destination,source,n,kind in entries))
                            register(0x1000,command)
                            return struct.unpack('<i',read(0xd0000000+command+16,4))[0]
                        for kind in (0,1):
                            # The RAM destination deliberately uses padded rows;
                            # source remains the old-pitch physical boot bytes.
                            destination=0x100000 if not kind else 0x400000
                            base=0xd0000000+destination if not kind else destination
                            pitch=2048 if not kind else 4096
                            write(base,b'\x55'*(pitch*300+32))
                            assert boot_read([(destination+y*pitch,y*2048,2048,kind) for y in range(300)])==512*300*4
                            copied=read(base,pitch*300+32)
                            rgb=b''.join(bytes((row[x+2],row[x+1],row[x])) for y in range(300)
                                         for row in [copied[y*pitch:y*pitch+2048]] for x in range(0,2048,4))
                            fitted(rgb,512,300)
                            assert copied[-32:]==b'\x55'*32
                            if kind: assert all(copied[y*pitch+2048:(y+1)*pitch]==b'\x55'*2048 for y in range(300))
                        # All destinations must remain untouched if any entry
                        # crosses the visible row, overlaps the packet or has an
                        # unknown memory kind. Rejection permits native fallback.
                        for bad in [(0x100100,2044,8,0),(command,0,4,0),(0x100100,0,4,2)]:
                            write(0xd0100000,b'\x55'*512)
                            assert boot_read([(0x100000,0,4,0),bad])<0
                            assert read(0xd0100000,512)==b'\x55'*512
                        print('PASS: bootstrap reads the completed fitted frame into RAM/VRAM; padding and failed-request destinations stay intact')
                    if args.boot_handoff_delay:
                        if args.boot_unpublished_write:
                            # A real boot console can still change a sampled
                            # old-pitch row (spinner/initialization) after SVGA
                            # mode negotiation. Difference is NOT publication
                            # of a coherent new-pitch frame. The former timer
                            # mistook this for repaint and exposed striped VRAM.
                            write(0xd0000000+32*320*4+64*4,b'\x44\x33\x22\xff')
                        # Elapsed time cannot publish either unchanged or
                        # partially written bytes under a new row pitch.
                        time.sleep(args.boot_handoff_delay)
                        delayed = frame('svga-delayed-before-repaint')
                        assert delayed.startswith(b'P6\n512 300\n255\n'), delayed[:100]
                        fitted(delayed.split(b'\n',3)[3],512,300)
                        print('PASS: delayed boot preserves one fitted image without any new-mode repaint')
                        if args.boot_unpublished_write:
                            # Restore this fixture's original old-layout bytes
                            # before its separate queued-UPDATE/multi-mode cases.
                            # Those UPDATEs intentionally declare completion;
                            # they are not the unpublished timer scenario above.
                            write(0xd0000000,boot)
                    # Several mode requests may precede the first repaint.
                    # Always retain the original, not an already scaled image
                    # or VRAM read using the intermediate mode's pitch.
                    for width,height in ((640,400),(320,512)):
                        register(2,width)
                        register(3,height)
                        for attempt in range(2):
                            write(0xe0000008, struct.pack('<2I',16,16))
                            write(0xe0000010, struct.pack('<5I',1,0,0,width,height))
                            access('writel 0xe0000008 36')
                            register(21,1)
                            image=frame(f'repeated-{width}-{height}-{attempt}')
                            assert image.startswith(f'P6\n{width} {height}\n255\n'.encode())
                            fitted(image.split(b'\n',3)[3],width,height)
                    print('PASS: repeated mode requests preserve original pixels and aspect, including portrait fit')
                    write(0xd0000000, struct.pack('<I', 0xff224488)*(width*height))
                    write(0xe0000008, struct.pack('<2I', 16, 16))
                    write(0xe0000010, struct.pack('<5I', 1, 0, 0, width, height))
                    access('writel 0xe0000008 36')
                    register(21, 1)
                    assert frame('svga-repainted').endswith(bytes.fromhex('224488')*width*height)
                    if args.boot_framebuffer_read:
                        assert read_register(0x1019)==0, 'Boot read remained active after a completed repaint'
                    print('PASS: completed new-mode pixels become visible without waiting for a timer')
                    return
                width, height = 320, 200
                for index, value in ((0, 0x90000002), (2, width), (3, height), (7, 32), (1, 1)):
                    register(index, value)
                write(0xe0000000, struct.pack('<4I', 16, 0x10000, 16, 16))
                register(20, 1)
                # Initialize the console's new mode before testing a completed
                # update; its first invalidation intentionally retains pixels.
                frame('initialized-mode')
                def publish():
                    write(0xe0000010, struct.pack('<5I', 1, 0, 0, width, height))
                    access('writel 0xe0000008 36')
                    register(21, 1)
                    # Reuse the same tiny FIFO only after native SYNC consumed it.
                    write(0xe0000008, struct.pack('<2I', 16, 16))
                write(0xd0000000, struct.pack('<I', 0xff224488)*(width*height))
                publish()
                first = frame('published')
                write(0xd0000000, struct.pack('<I', 0xff66aacc)*(width*height))
                pending = frame('not-published')
                assert pending == first, 'In-progress guest VRAM leaked into scanout'
                publish()
                second = frame('next-published')
                assert second != first, 'Completed next frame was not delivered'
                assert first.endswith(bytes.fromhex('224488')*width*height)
                assert second.endswith(bytes.fromhex('66aacc')*width*height)
                print('PASS: unpublished guest writes cannot alter completed scanout')
                # The native source-to-framebuffer copy must finish in one
                # device transaction, before the guest publishes UPDATE. A
                # rejected vector must not write even its valid first entry.
                source, packet = 8*1024*1024, 12*1024*1024
                pixels=struct.pack('<I',0xff335599)*(width*height)
                write(0xd0000000+source,pixels)
                def blit(entries):
                    size=sum(entry[2] for entry in entries)
                    header=struct.pack('<4IiI',0x56474c31,9,0,size,-1,len(entries))
                    table=b''.join(struct.pack('<Q4I',src,dst,length,kind,0)
                                   for src,dst,length,kind in entries)
                    write(0xd0000000+packet,header+table)
                    register(0x1000,packet)
                    return int(access(f'readl {0xd0000000+packet+16:#x}').split()[1],16)
                assert blit([(source,0,len(pixels),0)])==len(pixels)
                assert frame('native-not-published')==second
                publish()
                native=frame('native-published')
                assert native.endswith(bytes.fromhex('335599')*width*height)
                rejected=blit([(source,0,4,0),(source,len(pixels),4,0)])
                assert rejected & 0x80000000, 'Invalid native vector was accepted'
                publish()
                assert frame('native-rejected')==native
                print('PASS: native framebuffer blit completes before UPDATE; invalid vectors write nothing')
            finally:
                for connection in connections:
                    connection.close()
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
