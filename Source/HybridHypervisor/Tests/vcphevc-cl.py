#!/usr/bin/env python3
"""Exact 29-byte CL-shift neighborhood. prepare compiles/inspects only; run explicitly executes existing runners.

prepare VERIFIED_VCPHEVC NEW_DIRECTORY
run FIXTURE ENGINE_RUN FLATRUN NEW_RESULTS [--iterations 100000] [--runs 3]

Both shifts and the following XOR/load/store/ADD are retained. Synthetic tables, not captured decoder state.
Fast flags may retain the first shift's work too: by-CL decoder liveness is shared by both engine modes.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('vcp_sar_shared', HERE/'vcphevc-sar.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)  # Definitions only: no benchmark, preparation, or subprocess starts during import.
BODY = bytes.fromhex('49d3e74c8d58ff49d3e44c31d3418a943600010000438854287b4901c9')
MASK = (1 << 64)-1


def verify_decoder(path):
    raw = path.read_bytes()
    if len(raw) != 4528656 or shared.sha(raw) != shared.VERIFIED_DECODER or raw[:4] != b'\xca\xfe\xba\xbe':
        raise ValueError('not the verified Mojave universal decoder')
    slices = [struct.unpack_from('>IIIII',raw,8+i*20) for i in range(struct.unpack_from('>I',raw,4)[0])]
    matches = [item for item in slices if item[:2] == (0x1000007,3)]
    if len(matches) != 1:
        raise ValueError('expected baseline x86_64 slice, never x86_64h')
    _,_,offset,length,_ = matches[0]
    image = raw[offset:offset+length]
    segments,_ = shared.macho(image)
    if shared.at_address(image,segments,0xd92bd,len(BODY)) != BODY:
        raise ValueError('CL fragment bytes differ')
    return {'decoder_sha256':shared.sha(raw),'x86_64_slice_offset':offset,'x86_64_slice_sha256':shared.sha(image),
            'fragment_address':'0xd92bd','fragment_end':'0xd92da','fragment_hex':BODY.hex()}


def verify_fixture(path):
    raw = path.read_bytes()
    segments,symbols = shared.macho(raw)
    mapped = {name:address for name,address,size,offset,count in segments if name != '__LINKEDIT'}
    if mapped != {'__TEXT':shared.TEXT,'__DATA':shared.DATA}:
        raise ValueError('unexpected fixture mapping; refusing fixed-address runner')
    for prefix in ('_vcp_cl_case','_vcp_cl_batch'):
        begin,end = symbols[prefix+'_body'],symbols[prefix+'_end']
        if end-begin != len(BODY) or shared.at_address(raw,segments,begin,len(BODY)) != BODY:
            raise ValueError('compiled CL body differs from verified bytes')
    return {'fixture_sha256':shared.sha(raw),'text_address':hex(shared.TEXT),'data_address':hex(shared.DATA),
            'fragment_hex':BODY.hex(),'case_body':hex(symbols['_vcp_cl_case_body']),
            'batch_body':hex(symbols['_vcp_cl_batch_body'])}


def prepare(decoder,destination):
    provenance = verify_decoder(decoder)
    destination.mkdir(parents=True,exist_ok=False)
    x86 = HERE/'x86'
    flags = ['xcrun','clang','-target','x86_64-apple-macos10.9','-march=x86-64','-O2','-ffreestanding',
             '-fno-builtin','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
             '-fno-exceptions','-mno-red-zone','-fno-vectorize','-fno-slp-vectorize','-I',str(x86),
             '-Wno-incompatible-library-redeclaration','-Wno-format']
    commands,objects = [],[]
    for name in ('t_vcphevc_cl.c','t_vcphevc_cl.s','flatlibc.c'):
        obj = destination/(name+'.o')
        command = [*flags,'-c',str(x86/name),'-o',str(obj)]
        commands.append(command)
        subprocess.run(command,check=True,capture_output=True,text=True,timeout=30)
        objects.append(str(obj))
    image = destination/'t_vcphevc_cl'
    command = ['xcrun','ld','-arch','x86_64','-preload','-e','_flat_start','-no_uuid','-pagezero_size','0',
               '-segaddr','__TEXT',hex(shared.TEXT),'-segaddr','__DATA',hex(shared.DATA),'-o',str(image),*objects]
    commands.append(command)
    subprocess.run(command,check=True,capture_output=True,text=True,timeout=30)
    provenance.update(verify_fixture(image),commands=commands,executed=False,
                      note='Compiled/statically verified only. No Engine/HVF/Rosetta or guest execution.')
    (destination/'prepare.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(f'PREPARED exact29byte CL fixture without execution: {image}')


def fields(seed):
    value = seed & MASK
    shift = value & 63
    a,b = (value*3-1)&MASK,value&255
    result = (a+b)&MASK
    flags = (1 if result<a else 0) | (4 if bin(result&255).count('1')%2 == 0 else 0)
    flags |= (a^b^result)&16
    flags |= (64 if result==0 else 0) | (128 if result>>63 else 0)
    flags |= 2048 if (~(a^b)&(a^result))>>63&1 else 0
    byte = ((value&255)*73+19)&255
    return ((((value*5+0x13579)&MASK)<<shift)&MASK,(((value*3-0x2468)&MASK)<<shift)&MASK,
            (value-1)&MASK,(~value ^ (value*9))&MASK,((0x9234567800000000^value)&~255)|byte,
            result,flags,byte)


def expected_hash(iterations,seed):
    result = 0xcbf29ce484222325
    for i in range(iterations):
        for item in fields(seed+i):
            result = ((result^item)*0x1000193)&MASK
    return f'{result:016x}'


def parse_result(stdout,iterations,expected):
    rows = re.findall(r'^PASS vcphevc-cl cases=4096 iterations=([0-9]+) elapsed_us=([0-9]+) hash=([0-9a-f]{16}) expected=([0-9a-f]{16})$',stdout,re.M)
    if len(rows)!=1 or int(rows[0][0])!=iterations or rows[0][2:]!=(expected,expected) or int(rows[0][1])<=0:
        raise ValueError('CL fixture failed independent cases/iteration/hash/duration validation')
    return {'iterations':iterations,'elapsed_us':int(rows[0][1]),'hash':expected,
            'ns_per_fragment_with_wrapper':int(rows[0][1])*1000/iterations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action',required=True)
    prep = sub.add_parser('prepare')
    prep.add_argument('decoder',type=Path)
    prep.add_argument('output',type=Path)
    bench = sub.add_parser('run')
    for name in ('image','engine','flatrun','output'):
        bench.add_argument(name,type=Path)
    bench.add_argument('--iterations',type=int,default=100000)
    bench.add_argument('--runs',type=int,default=3)
    args = parser.parse_args()
    if args.action=='prepare':
        prepare(args.decoder.resolve(),args.output.resolve())
    else:
        if not 1<=args.iterations<=1000000 or not 1<=args.runs<=3:
            parser.error('iterations1..1000000 and runs1..3 required')
        shared.verify_fixture,shared.expected_hash,shared.parse_result = verify_fixture,expected_hash,parse_result
        shared.run(args.image.resolve(),args.engine.resolve(),args.flatrun.resolve(),args.output.resolve(),args.iterations,args.runs)


if __name__=='__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f'FAIL CL preparation: {error.stderr}') from error
