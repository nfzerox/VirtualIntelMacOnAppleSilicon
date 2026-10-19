"""Dependency-free Mach-O reader for guarded guest patches (Python 2.7/3)."""
from __future__ import print_function
import struct
import re


def require(condition, message):
    if not condition:
        raise ValueError(message)


def thin(data):
    if data[:4] in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
        wide = data[:4] == b'\xca\xfe\xba\xbf'
        count = struct.unpack_from('>I', data, 4)[0]
        stride = 32 if wide else 20
        require(8 + count * stride <= len(data), 'truncated fat header')
        matches = []
        for i in range(count):
            p = 8 + i * stride
            cpu = struct.unpack_from('>I', data, p)[0]
            offset, size = struct.unpack_from('>QQ' if wide else '>II', data, p+8)
            require(offset + size <= len(data), 'truncated fat slice')
            if cpu == 0x1000007:
                matches.append(data[offset:offset+size])
        require(len(matches) == 1, 'expected one x86_64 slice')
        data = matches[0]
    require(len(data) >= 32 and struct.unpack_from('<II', data) ==
            (0xfeedfacf, 0x1000007), 'expected x86_64 Mach-O')
    return data


def replace_x86_64(original, replacement):
    """Combine a new 64-bit implementation with every original other slice."""
    replacement = thin(replacement)
    require(original[:4] in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'),
            'original universal image required to preserve 32-bit compatibility')
    thin(original)  # Validate bounds and unique x86_64 slice first.
    wide = original[:4] == b'\xca\xfe\xba\xbf'
    count = struct.unpack_from('>I', original, 4)[0]
    stride = 32 if wide else 20
    records, chunks = [], []
    cursor = 8 + count * stride
    has_i386 = False
    for i in range(count):
        p = 8 + i * stride
        cpu, subtype = struct.unpack_from('>II', original, p)
        offset, size = struct.unpack_from('>QQ' if wide else '>II', original, p+8)
        alignment = struct.unpack_from('>I', original, p+(24 if wide else 16))[0]
        require(alignment <= 20, 'unsupported fat alignment')
        has_i386 |= cpu == 7
        content = replacement if cpu == 0x1000007 else original[offset:offset+size]
        aligned = (cursor + (1 << alignment) - 1) & ~((1 << alignment)-1)
        chunks.append(b'\0'*(aligned-cursor)+content)
        record = struct.pack('>IIQQII',cpu,subtype,aligned,len(content),alignment,0) if wide else \
            struct.pack('>IIIII',cpu,subtype,aligned,len(content),alignment)
        records.append(record); cursor = aligned + len(content)
    require(has_i386, 'original i386 slice is missing; restore a stock backup first')
    return original[:4]+struct.pack('>I',count)+b''.join(records)+b''.join(chunks)


class Image(object):
    def __init__(self, data):
        self.original = data
        self.data = thin(data)
        self.segments, self.sections, self.symbols = [], [], {}
        count, size = struct.unpack_from('<II', self.data, 16)
        self.command_end = 32 + size
        require(self.command_end <= len(self.data), 'invalid command area')
        p, symtab = 32, None
        for unused in range(count):
            require(p+8 <= self.command_end, 'truncated command')
            cmd, length = struct.unpack_from('<II', self.data, p)
            require(length >= 8 and p+length <= self.command_end, 'invalid command')
            if cmd == 0x19:
                require(length >= 72, 'truncated segment')
                name = self.data[p+8:p+24].rstrip(b'\0')
                va, vsize, off, fsize, maximum, protection, nsects = struct.unpack_from('<QQQQiiI', self.data, p+24)
                require(off+fsize <= len(self.data) and 72+nsects*80 <= length, 'invalid segment bounds')
                self.segments.append((name, va, vsize, off, fsize, protection))
                for i in range(nsects):
                    q = p+72+i*80
                    section = self.data[q:q+16].rstrip(b'\0')
                    addr, size, offset = struct.unpack_from('<QQI', self.data, q+32)
                    flags = struct.unpack_from('<I', self.data, q+64)[0]
                    self.sections.append((name, section, addr, size, offset, flags))
            elif cmd == 2:
                require(length >= 24, 'invalid symtab command')
                symtab = struct.unpack_from('<IIII', self.data, p+8)
            p += length
        require(p == self.command_end, 'inconsistent command size')
        text = [s for s in self.sections if s[:2] == (b'__TEXT', b'__text')]
        require(len(text) == 1, 'missing unique text section')
        self.text = text[0]
        require(self.text[4]+self.text[3] <= len(self.data), 'invalid text bounds')
        if symtab:
            off, count, strings, size = symtab
            require(off+count*16 <= len(self.data) and strings+size <= len(self.data), 'invalid symbols')
            for i in range(count):
                index, kind, sect, desc, address = struct.unpack_from('<IBBHQ', self.data, off+i*16)
                if kind & 0xe0 or kind & 0xe != 0xe:
                    continue
                require(index < size, 'invalid symbol name')
                end = self.data.find(b'\0', strings+index, strings+size)
                require(end != -1, 'unterminated symbol')
                name = self.data[strings+index:end].decode('ascii', 'replace')
                self.symbols.setdefault(name, set()).add(address)

    def preserving_slices(self, patched):
        """Replace an equal-size x86_64 slice; leave all other slices intact."""
        require(len(patched) == len(self.data), 'patch changed slice size')
        if self.original[:4] not in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
            return patched
        wide = self.original[:4] == b'\xca\xfe\xba\xbf'
        count = struct.unpack_from('>I', self.original, 4)[0]
        stride = 32 if wide else 20
        result = bytearray(self.original)
        for i in range(count):
            p = 8 + i * stride
            if struct.unpack_from('>I', self.original, p)[0] == 0x1000007:
                offset, size = struct.unpack_from('>QQ' if wide else '>II', self.original, p+8)
                require(size == len(patched), 'fat slice size mismatch')
                result[offset:offset+size] = patched
                return result
        raise ValueError('missing x86_64 slice')

    def offset(self, address, size=1):
        matches = [off+address-va for name, va, vs, off, fs, prot in self.segments
                   if va <= address and address+size <= va+fs]
        require(len(matches) == 1, 'address is not uniquely file-backed: %#x' % address)
        return matches[0]

    def read(self, address, size):
        p = self.offset(address, size)
        return self.data[p:p+size]

    def writable(self, address):
        require(any(va <= address < va+vs and prot & 2
                    for name, va, vs, off, fs, prot in self.segments), 'expected writable data reference')

    def symbol(self, name):
        values = self.symbols.get(name, set())
        require(len(values) == 1, 'missing/ambiguous symbol: '+name)
        return next(iter(values))

    def function(self, name):
        start = self.symbol(name)
        low, high = self.text[2], self.text[2]+self.text[3]
        require(low <= start < high, 'symbol outside executable text')
        later = [a for values in self.symbols.values() for a in values if start < a <= high]
        return start, min(later) if later else high

    def matches(self, pattern, bounds=None):
        start, end = bounds or (self.text[2], self.text[2]+self.text[3])
        return [start+m.start() for m in re.finditer(pattern, self.read(start, end-start), re.S)]

    def rel(self, instruction, displacement, length):
        return instruction + length + struct.unpack('<i', self.read(instruction+displacement, 4))[0]

    def cave(self, size):
        text = [s for s in self.segments if s[0] == b'__TEXT']
        require(len(text) == 1 and text[0][3] == 0 and text[0][5] & 4, 'no executable header padding')
        offset = (self.command_end+256+15) & ~15
        first = min(s[4] for s in self.sections if s[0] == b'__TEXT' and s[4])
        require(offset+size <= first and self.data[offset:offset+size] == b'\0'*size,
                'insufficient empty executable header padding')
        return text[0][1]+offset


def relative(op, address, target, suffix=b''):
    return op + struct.pack('<i', target-address-len(op)-4-len(suffix)) + suffix


def one(values, label):
    require(len(values) == 1, 'unknown/ambiguous '+label+': %d matches' % len(values))
    return values[0]


def save(path, data):
    with open(path, 'wb') as stream:
        stream.write(data)


if __name__ == '__main__':
    import sys
    if len(sys.argv) == 5 and sys.argv[1] == 'replace-x86_64':
        with open(sys.argv[2], 'rb') as stream: original = stream.read()
        with open(sys.argv[3], 'rb') as stream: replacement = stream.read()
        save(sys.argv[4], replace_x86_64(original, replacement))
    else:
        require(len(sys.argv) == 4 and sys.argv[1] == 'thin', 'usage: pvg_macho.py thin INPUT OUTPUT | replace-x86_64 ORIGINAL REPLACEMENT OUTPUT')
        with open(sys.argv[2], 'rb') as stream:
            data = thin(stream.read())
        save(sys.argv[3], data)
