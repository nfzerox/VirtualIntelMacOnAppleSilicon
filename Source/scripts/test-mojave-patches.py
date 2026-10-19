#!/usr/bin/env python3
"""Portable synthetic relocation, idempotence and refusal tests. No guest writes."""
import importlib.util
import struct
import tempfile
from pathlib import Path
from pvg_macho import relative, thin, replace_x86_64

root = Path(__file__).resolve().parent
def module(name):
    spec = importlib.util.spec_from_file_location(name, root/(name+'.py'))
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    return mod
core = module('patch-mojave-coredisplay')
sky = module('patch-mojave-skylight')


def fixture(kind, base, shift=0, bad=None, native_metadata=False):
    data = bytearray(0x5000)
    symbols = {}
    def put(off, value): data[off:off+len(value)] = value
    def rel(op, off, target, suffix=b''): return relative(op, base+off, base+target, suffix)
    if kind == 'core':
        start = 0x1100+shift
        code = b'\x55\x48\x89\xe5'
        for n, (name, bit) in enumerate(((b'AppleIntelFramebuffer',1),(b'NVDA',2),(b'AMDFramebuffer',4))):
            string = 0x2100+64*n
            put(string, name+b'\0')
            code += rel(b'\x48\x8d\x3d', start+len(code), string)
            code += rel(b'\xe8', start+len(code), 0x1800)
            code += b'\x84\xc0\x74\x07'
            code += rel(b'\x80\x0d', start+len(code), 0x3100, bytes([bit]))
        code += b'\x5d\xc3'; put(start, code)
        if bad == 'ambiguous':
            duplicate = bytearray(code)
            for n in range(3):
                for field in (7,12,22):
                    p = n*23+field
                    struct.pack_into('<i', duplicate, p, struct.unpack_from('<i',duplicate,p)[0]-0x80)
            put(start+0x80, duplicate)
        if bad == 'unknown': data[0x2100] = ord('X')
        for n, p in enumerate((0x1400+shift, 0x1600+shift)):
            v = rel(b'\x80\x3d', p, 0x3150+n, b'\0') + b'\x74\x51'
            v += rel(b'\x48\x83\x3d', p+len(v), 0x3180, b'\xff') + b'\x75\x4a'
            v += rel(b'\x0f\xb6\x05', p+len(v), 0x3160+n)
            v += b'\x89\xc2\x83\xca\x02\x31\xc9'
            v += rel(b'\x38\x0d', p+len(v), 0x3190)
            v += b'\x0f\x44\xd0\x89\xd0\x83\xc8\x04'
            v += rel(b'\x38\x0d', p+len(v), 0x3191)
            v += rel(b'\x8b\x0d', p+len(v), 0x3100)
            v += b'\x0f\x44\xc2\x85\xc9\x74\x11\x21\xc8\x39\xc8\x75\x0b'
            v += rel(b'\x80\x3d', p+len(v), 0x3150+n, b'\0') + b'\x0f\x95\xc0\xc3'
            put(p, v)
    else:
        init, report, select, metal = [a+shift for a in (0x1100,0x1400,0x1500,0x1600)]
        symbols = {'_CGXInitializeGL':init, '__XServerMightHaveAcceleration':report,
                   '__ZL27activate_compositor_defaultv':select, '_WSMetalCompositorEnabled':metal,
                   '__ZZ15WSGVAInitializeE4once':0x3100, '__cglsContext':0x3120}
        put(init+0x10, rel(b'\xe9', init+0x10, init+0x80))
        put(init+0x40, rel(b'\xe8', init+0x40, select))
        put(init+0x80, rel(b'\x48\x83\x3d', init+0x80, 0x3100, b'\xff')+b'\x75\x29')
        put(report+0x13, rel(b'\x48\x83\x3d', report+0x13, 0x3120, b'\0')+b'\x0f\x95\x46\x24')
        if bad == 'ambiguous': put(init+0x50, rel(b'\xe8',init+0x50,select))
        if bad == 'unknown': data[report+0x13] = 0x90
    commands = []
    def segment(name, off, size, protection, sections):
        cmd = struct.pack('<II16sQQQQiiII',0x19,72+80*len(sections),name,base+off,size,off,size,protection,protection,len(sections),0)
        for name_, addr, length in sections:
            cmd += struct.pack('<16s16sQQIIIIIIII',name_,name,base+addr,length,addr,0,0,0,0,0,0,0)
        commands.append(cmd)
    segment(b'__TEXT',0,0x3000,5,[(b'__text',0x1000,0x1000),(b'__cstring',0x2000,0x400)])
    segment(b'__DATA',0x3000,0x1000,3,[(b'__data',0x3000,0x1000)])
    segment(b'__LINKEDIT',0x4000,0x1000,1,[])
    names = b'\0'
    for n,(name,addr) in enumerate(symbols.items()):
        put(0x4000+16*n,struct.pack('<IBBHQ',len(names),14,1,0,base+addr))
        names += name.encode()+b'\0'
    put(0x4800,names)
    commands.append(struct.pack('<6I',2,24,0x4000,len(symbols),0x4800,len(names)))
    commands.append(struct.pack('<II',0x1b,24)+bytes([shift//0x10+1])*16)
    if native_metadata:
        # Native signing requires the string table to be the last live LINKEDIT
        # data. Keep split-segment records before it, as in real Apple images.
        commands.append(struct.pack('<4I',0x1e,16,0x4700,3))
        commands.append(struct.pack('<4I',0x1d,16,0x4d00,0x300))
        put(0x4700,b'\x01\0\0')
    if kind == 'sky':
        dependency=b'/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit\0'
        size=(24+len(dependency)+7)&~7
        commands.append((struct.pack('<6I',12,size,24,0,0,0)+dependency).ljust(size,b'\0'))
    all_commands = b''.join(commands)
    put(0,struct.pack('<8I',0xfeedfacf,0x1000007,3,6,len(commands),len(all_commands),0,0))
    put(32,all_commands)
    if bad == 'padding': put(32+len(all_commands), b'X'*1024)
    return bytes(data)


with tempfile.TemporaryDirectory(prefix='pvg-mojave-patches-') as folder:
    source, output, twice = [Path(folder)/n for n in ('input','output','twice')]
    for kind, patcher in [('core',core.patch),('sky',lambda a,b:sky.patch(a,b,True))]:
        for base,shift in [(0,0),(0x7fff20000000,0x100),(0x100000000,0x200)]:
            raw = fixture(kind,base,shift)
            source.write_bytes(raw); patcher(str(source),str(output)); patcher(str(output),str(twice))
            assert output.read_bytes() == twice.read_bytes()
            # Universal wrapper; no command-line developer tools required.
            other = b'i386 slice must survive byte-for-byte'.ljust(4096,b'\xa5')
            fat = struct.pack('>12I',0xcafebabe,2,0x1000007,3,4096,len(raw),12,
                              7,3,4096+len(raw),len(other),12)
            universal = fat.ljust(4096,b'\0')+raw+other
            source.write_bytes(universal)
            patcher(str(source),str(twice))
            result = twice.read_bytes()
            assert thin(result)==output.read_bytes()
            assert result[:4096]==universal[:4096] and result[-len(other):]==other
            assert len(result)==len(universal)
            replacement=output.read_bytes()+b'new length'
            merged=replace_x86_64(universal,replacement)
            assert thin(merged)==replacement
            other_offset,other_size=struct.unpack_from('>II',merged,36)
            assert merged[other_offset:other_offset+other_size]==other
            print('PASS',kind,'relocation/UUID variation/universal/idempotence',hex(base),shift)
        for bad in ('unknown','ambiguous','padding'):
            source.write_bytes(fixture(kind,0,bad=bad)); output.write_bytes(b'untouched')
            try: patcher(str(source),str(output))
            except ValueError: pass
            else: raise AssertionError('accepted '+bad)
            assert output.read_bytes()==b'untouched'
            print('PASS',kind,'refused',bad,'without modifying output')
    for malformed in (b'',b'\xcf\xfa\xed\xfe',struct.pack('>II',0xcafebabe,100)):
        try: thin(malformed)
        except ValueError: pass
        else: raise AssertionError('accepted malformed image')
print('PASS malformed inputs')
