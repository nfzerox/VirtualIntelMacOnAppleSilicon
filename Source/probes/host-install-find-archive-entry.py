#!/usr/bin/env python3
"""Developer-only original-entry extraction without reinstalling an OS.

Index independent PBZX blocks, skip file bodies by Apple's header parser, and
decode only blocks needed by headers/the requested entry. Never writes a VM.
"""
import argparse
from bisect import bisect_right
import ctypes as C
import lzma
from pathlib import Path
import re
import struct
import zipfile


class Payload:
    def __init__(self, source):
        self.source=source;self.blocks=[];self.position=0;self.cached=-1;self.data=b''
        magic,maximum=struct.unpack('>4sQ',source.read(12))
        if magic!=b'pbzx':raise ValueError('Not PBZX')
        logical=0
        while header:=source.read(16):
            length,stored=struct.unpack('>QQ',header)
            if not 0<stored<=length<=maximum<=16*1024*1024:raise ValueError('Invalid PBZX block')
            self.blocks.append((logical,source.tell(),length,stored))
            logical+=length;source.seek(stored,1)
        self.length=logical;self.starts=[b[0] for b in self.blocks]

    def read(self, count):
        result=[]
        while count and self.position<self.length:
            index=bisect_right(self.starts,self.position)-1
            logical,at,length,stored=self.blocks[index]
            if index!=self.cached:
                self.source.seek(at);data=self.source.read(stored)
                self.data=lzma.decompress(data) if data.startswith(b'\xfd7zXZ\x00') else data
                if len(self.data)!=length:raise ValueError('Truncated PBZX block')
                self.cached=index
            offset=self.position-logical;take=min(count,length-offset)
            result.append(self.data[offset:offset+take]);self.position+=take;count-=take
        return b''.join(result)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zip',type=Path);parser.add_argument('path')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    aa=C.CDLL('/usr/lib/libAppleArchive.dylib')
    aa.AAHeaderCreateWithEncodedData.argtypes=[C.c_size_t,C.c_void_p];aa.AAHeaderCreateWithEncodedData.restype=C.c_void_p
    aa.AAHeaderGetKeyIndex.argtypes=[C.c_void_p,C.c_uint32];aa.AAHeaderGetKeyIndex.restype=C.c_int
    aa.AAHeaderGetFieldString.argtypes=[C.c_void_p,C.c_uint32,C.c_size_t,C.c_void_p,C.c_void_p]
    aa.AAHeaderGetPayloadSize.argtypes=[C.c_void_p];aa.AAHeaderGetPayloadSize.restype=C.c_uint64
    aa.AAHeaderDestroy.argtypes=[C.c_void_p]
    with zipfile.ZipFile(args.zip) as archive:
        members=sorted(n for n in archive.namelist() if re.fullmatch(r'payload\.\d{3}',n.rsplit('/',1)[-1]))
        for member in members:
            with archive.open(member) as source:
                stream=Payload(source)
                while first:=stream.read(6):
                    if len(first)!=6:raise ValueError('Truncated header')
                    length=struct.unpack_from('<H',first,4)[0]
                    if length<6:raise ValueError('Invalid header')
                    encoded=first+stream.read(length-6)
                    header=aa.AAHeaderCreateWithEncodedData(length,encoded)
                    if not header:raise ValueError('Invalid Apple Archive header')
                    try:
                        index=aa.AAHeaderGetKeyIndex(header,int.from_bytes(b'PAT\0','little'))
                        name=C.create_string_buffer(4096)
                        if index<0 or aa.AAHeaderGetFieldString(header,index,len(name),name,None):raise ValueError('Missing path')
                        size=aa.AAHeaderGetPayloadSize(header)
                    finally:aa.AAHeaderDestroy(header)
                    if name.value.decode()==args.path:
                        with args.output.open('xb') as out:
                            out.write(encoded)
                            while size:
                                data=stream.read(min(size,1024*1024))
                                if not data:raise ValueError('Truncated entry')
                                out.write(data);size-=len(data)
                        print(member,flush=True);return
                    stream.position+=size
            print('Scanned',member,flush=True)
    raise SystemExit('Entry not found')


if __name__=='__main__':main()
