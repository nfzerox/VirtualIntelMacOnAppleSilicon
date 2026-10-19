#!/usr/bin/env python3
"""Native extraction safety and content checks in owned temporary directories."""
import gzip
import io
import json
import lzma
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

tool = Path(sys.argv[1]).resolve()
def archive(entries):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode='w', format=tarfile.USTAR_FORMAT) as stream:
        for name, kind, value in entries:
            entry = tarfile.TarInfo(name);entry.mode=0o755;entry.type=kind
            if kind == tarfile.REGTYPE:
                entry.size=len(value);stream.addfile(entry, io.BytesIO(value))
            else:
                if kind in (tarfile.SYMTYPE,tarfile.LNKTYPE): entry.linkname=value
                stream.addfile(entry)
    return raw.getvalue()

with tempfile.TemporaryDirectory(prefix='vim-extract-test-') as temporary:
    root = Path(temporary); source=root/'input.tar.gz'
    prefix=[('payload',tarfile.DIRTYPE,'')]
    valid=prefix+[
        ('payload/Test.framework',tarfile.DIRTYPE,''),
        ('payload/Test.framework/Versions/A/code',tarfile.REGTYPE,bytes(range(256))*1000),
        ('payload/Test.framework/Versions/Current',tarfile.SYMTYPE,'A'),
        ('payload/Test.framework/code',tarfile.SYMTYPE,'Versions/Current/code'),
        ('payload/'+('long-directory-name/'*8)+'file',tarfile.REGTYPE,b'long ustar path'),
    ]
    compressor=gzip.compress
    def run(data, name, success):
        source.write_bytes(compressor(data));destination=root/name
        result=subprocess.run([str(tool),'--extract',str(source),str(destination)],capture_output=True)
        assert (result.returncode==0)==success,result.stderr
        return destination,result
    destination,result=run(archive(valid),'valid',True)
    report=json.loads(result.stdout)
    assert report['Extracted'] is True and report['Installed'] is False
    assert (destination/'payload/Test.framework/code').read_bytes()==bytes(range(256))*1000
    assert (destination/'payload/Test.framework/code').is_symlink()
    # Existing directory must never be reused or clobbered.
    result=subprocess.run([str(tool),'--extract',str(source),str(destination)],capture_output=True)
    assert result.returncode!=0
    cases={
        'traversal':prefix+[('payload/../outside',tarfile.REGTYPE,b'bad')],
        'absolute':prefix+[('/outside',tarfile.REGTYPE,b'bad')],
        'hardlink':prefix+[('payload/link',tarfile.LNKTYPE,'outside')],
        'symlink-escape':prefix+[('payload/link',tarfile.SYMTYPE,'../outside')],
        'symlink-parent':prefix+[('payload/link',tarfile.SYMTYPE,'real'),('payload/link/file',tarfile.REGTYPE,b'bad')],
        'duplicate':prefix+[('payload/file',tarfile.REGTYPE,b'a'),('payload/file',tarfile.REGTYPE,b'b')],
    }
    for name, entries in cases.items():run(archive(entries),name,False)
    valid_bytes=archive(valid)
    run(valid_bytes[:700],'truncated',False)
    altered=bytearray(valid_bytes);altered[0]^=1
    run(altered,'bad-header-checksum',False)
    corrupted=bytearray(gzip.compress(valid_bytes));corrupted[-8]^=1
    source.write_bytes(corrupted)
    result=subprocess.run([str(tool),'--extract',str(source),str(root/'bad-gzip')],capture_output=True)
    assert result.returncode!=0
    source.write_bytes(gzip.compress(valid_bytes)[:-1])
    result=subprocess.run([str(tool),'--extract',str(source),str(root/'truncated-gzip-footer')],capture_output=True)
    assert result.returncode!=0
    source.write_bytes(valid_bytes)
    result=subprocess.run([str(tool),'--extract',str(source),str(root/'not-gzip')],capture_output=True)
    assert result.returncode!=0
    # Both envelopes run through the exact same restricted ustar/file writer.
    compressor=lzma.compress
    destination,result=run(valid_bytes,'valid-xz',True)
    assert (destination/'payload/Test.framework/code').read_bytes()==bytes(range(256))*1000
    assert (destination/'payload/Test.framework/code').is_symlink()
    for name, entries in cases.items():run(archive(entries),name+'-xz',False)
    run(valid_bytes[:700],'truncated-tar-xz',False)
    encoded=lzma.compress(valid_bytes)
    bad_xz={
        'truncated-xz':encoded[:-1],
        'truncated-xz-header':encoded[:6],
        'trailing-xz':encoded+b'garbage',
        'concatenated-xz':encoded+encoded,
        'unchecked-xz':lzma.compress(valid_bytes,check=lzma.CHECK_NONE),
        'oversized-dictionary-xz':lzma.compress(valid_bytes,filters=[{'id':lzma.FILTER_LZMA2,'dict_size':128*1024*1024}]),
    }
    damaged=bytearray(encoded);damaged[-16]^=1;bad_xz['corrupt-xz-index']=damaged
    damaged=bytearray(encoded);damaged[len(damaged)//2]^=1;bad_xz['corrupt-xz-data']=damaged
    for name, data in bad_xz.items():
        source.write_bytes(data)
        result=subprocess.run([str(tool),'--extract',str(source),str(root/name)],capture_output=True)
        assert result.returncode!=0,(name,result.stdout)
    assert not (root/'outside').exists()
print('PASS native gzip/XZ extraction: identical data/links, no overwrite, traversal/link/type/checksum/truncation refusal, XZ memory/check/footer/trailing-stream limits')
