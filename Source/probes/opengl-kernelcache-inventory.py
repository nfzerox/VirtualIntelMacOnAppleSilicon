#!/usr/bin/env python3
"""Read-only inventory of an actual thin legacy Apple LZSS kernelcache.

Format/decoder reference: vendored Lilu kern_compression.{hpp,cpp} (BSD-3-Clause,
vit9696; its LZSS implementation credits Apple kext_tools). Validates exact
decoded length and Apple's Adler32 before interpreting Mach-O/prelink metadata.
No installed kernel, cache, running guest or system file is modified.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib
import xml.etree.ElementTree as ET


def decode(data):
    if data[:8]!=b'complzss' or len(data)<384:raise ValueError('Expected a thin comp/lzss kernelcache (use lipo first)')
    checksum,size,compressed,version=struct.unpack_from('>IIII',data,8)
    # Lion's shipping cache uses version 0; later caches use version 1.
    # Both have the same MACH_COMP_HEADER/LZSS layout (also consumed by
    # OpenCore's ParseCompressedHeader). Keep the size/hash validation.
    if size>512*1024*1024 or compressed!=len(data)-384 or version not in (0,1):raise ValueError('Unsupported compression header')
    source=memoryview(data)[384:];cursor=0;flags=0;ring=bytearray(b' '*4096);write=4096-18;output=bytearray()
    while cursor<len(source) and len(output)<size:
        flags>>=1
        if not flags&0x100:flags=source[cursor]|0xff00;cursor+=1
        if flags&1:
            if cursor>=len(source):raise ValueError('Truncated literal')
            value=source[cursor];cursor+=1;output.append(value);ring[write]=value;write=(write+1)&4095
        else:
            if cursor+2>len(source):raise ValueError('Truncated match')
            low,high=source[cursor:cursor+2];cursor+=2;offset=low|((high&0xf0)<<4);length=(high&15)+3
            if len(output)+length>size:raise ValueError('Decoded match exceeds declared size')
            for index in range(length):
                value=ring[(offset+index)&4095];output.append(value);ring[write]=value;write=(write+1)&4095
    if len(output)!=size or zlib.adler32(output)!=checksum:raise ValueError('Kernelcache length/checksum mismatch')
    return output


def inventory(kernel):
    magic,_,_,_,count,commands,_,_=struct.unpack_from('<8I',kernel)
    if magic!=0xfeedfacf or 32+commands>len(kernel):raise ValueError('Expected a valid x64 Mach-O kernel')
    cursor=32
    for _ in range(count):
        command,size=struct.unpack_from('<II',kernel,cursor)
        if size<8 or cursor+size>32+commands:raise ValueError('Invalid load command')
        if command==0x19:
            sections=struct.unpack_from('<I',kernel,cursor+64)[0]
            if sections>(size-72)//80:raise ValueError('Invalid section count')
            for index in range(sections):
                section=cursor+72+index*80
                name,segment=struct.unpack_from('<16s16s',kernel,section)
                length,offset=struct.unpack_from('<QI',kernel,section+40)
                if name.rstrip(b'\0')==b'__info' and segment.rstrip(b'\0')==b'__PRELINK_INFO':
                    if offset+length>len(kernel):raise ValueError('Prelink info outside kernel')
                    # Prelink XML omits the normal plist preamble and uses
                    # Apple's ID/IDREF deduplication (including empty integer
                    # references). Read the specific inventory, not a lossy
                    # generic plistlib conversion of the entire graph.
                    root=ET.fromstring(kernel[offset:offset+length].rstrip(b'\0'))
                    identities={e.attrib['ID']:e for e in root.iter() if 'ID' in e.attrib}
                    def resolve(element):
                        return identities[element.attrib['IDREF']] if 'IDREF' in element.attrib else element
                    def field(dictionary,key):
                        children=list(resolve(dictionary))
                        for index in range(0,len(children)-1,2):
                            if resolve(children[index]).text==key:return resolve(children[index+1])
                        return None
                    if root.tag=='plist':root=root[0]
                    entries=field(root,'_PrelinkInfoDictionary')
                    if entries is None:raise ValueError('No prelink dictionary array')
                    result=set()
                    for entry in entries:
                        value=field(entry,'CFBundleIdentifier')
                        if value is not None and value.tag=='string':result.add(value.text)
                    return sorted(result)
        cursor+=size
    raise ValueError('No prelink info section')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('cache',type=Path);a=p.parse_args()
    data=a.cache.read_bytes();kernel=decode(data) if data[:4]==b'comp' else data
    print(json.dumps({'DecodedBytes':len(kernel),'IncludedKexts':inventory(kernel)},indent=2))

if __name__=='__main__':main()
