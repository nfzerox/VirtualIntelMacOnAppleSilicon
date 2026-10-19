#!/usr/bin/env python3
"""Synthetic Mach-O relocation/ambiguity tests; no system files are modified."""
import struct
import subprocess
import tempfile
import sys
from pathlib import Path
repo = Path(__file__).resolve().parent.parent
old_a = bytes.fromhex("0f44d089d083c804380d")
new_a = bytes.fromhex("0f44d089d083c814380d")
old_b = bytes.fromhex("4883f8ffb801000000bb080000000f44d8")
new_b = bytes.fromhex("4883f8ffb810000000bb080000000f44d8")

def fixture(textoff, duplicate=False, unknown=False):
    textsize=256; symoff=textoff+textsize; stroff=symoff+32
    names=[b"__ZN11CoreDisplay16UseIOPresentmentEv", b"_GetGPUVendorForFramebufferService"]
    strings=b"\0"+names[0]+b"\0"+names[1]+b"\0"
    data=bytearray(stroff+len(strings))
    data[:32]=struct.pack("<8I",0xfeedfacf,0x1000007,3,6,2,176,0,0)
    base=0x100000000
    data[32:104]=struct.pack("<II16sQQQQiiII",0x19,152,b"__TEXT",base,len(data),0,len(data),5,5,1,0)
    data[104:184]=struct.pack("<16s16sQQIIIIIIII",b"__text",b"__TEXT",base+textoff,textsize,textoff,0,0,0,0,0,0,0)
    data[184:208]=struct.pack("<6I",2,24,symoff,2,stroff,len(strings))
    data[symoff:symoff+16]=struct.pack("<IBBHQ",1,14,1,0,base+textoff)
    data[symoff+16:symoff+32]=struct.pack("<IBBHQ",len(names[0])+2,14,1,0,base+textoff+128)
    data[stroff:]=strings
    data[textoff+16:textoff+16+len(old_a)]=old_a if not unknown else b"\x90"*len(old_a)
    data[textoff+144:textoff+144+len(old_b)]=old_b
    # Same mask sequence in a different function must not create ambiguity.
    data[textoff+192:textoff+192+len(old_a)]=old_a
    if duplicate: data[textoff+48:textoff+48+len(old_a)]=old_a
    return bytes(data)

with tempfile.TemporaryDirectory(prefix="pvg-patch-test-") as folder:
    root=Path(folder); source=root/"input"; output=root/"output"
    def patch():
        # Optional compiled tool exercises exactly the same relocation cases.
        command = [sys.argv[1]] if len(sys.argv)>1 else ["/usr/bin/perl",str(repo/"scripts/patch-guest-image.pl")]
        if output.exists(): output.unlink()
        return subprocess.run(command+["coredisplay",str(source),str(output)],capture_output=True)
    for position in (512,4096):
        source.write_bytes(fixture(position)); result=patch()
        assert result.returncode==0, result.stderr
        expected=bytearray(source.read_bytes())
        expected[position+16:position+16+len(new_a)]=new_a
        expected[position+144:position+144+len(new_b)]=new_b
        assert output.read_bytes()==bytes(expected)
        source.write_bytes(output.read_bytes()); assert patch().returncode==0
        assert output.read_bytes()==bytes(expected)
        print("PASS symbol-scoped relocation and idempotence",position)
    for label,options in (("ambiguous",dict(duplicate=True)),("unknown",dict(unknown=True))):
        source.write_bytes(fixture(512,**options)); assert patch().returncode!=0
        print("PASS refuse",label)
