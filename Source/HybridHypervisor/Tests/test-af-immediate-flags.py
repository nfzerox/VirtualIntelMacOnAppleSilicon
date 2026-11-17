#!/usr/bin/env python3
"""Prepare/check the data-only production decoder/emitter AF regression.

Never compiles/runs binaries. Copies only named production inputs; baseline is
reconstructed from the reviewed candidate AF block, with every other byte kept.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex

PREFIX='''if (imm >= 0 && imm < 4096) {
                if (live & F_A) {
                    u32 enc;'''
CANDIDATE='''if (logimm((u64)imm & 0x10, 1, &enc))
                        E(0xD2000000u | enc | dst << 5 | R_AF);
                    else
                        mov_reg(1, R_AF, dst);'''
BASELINE='''if (imm && logimm((u64)imm, 1, &enc))
                        E(0xD2000000u | enc | dst << 5 | R_AF);
                    else if (!imm)
                        mov_reg(1, R_AF, dst);
                    else {
                        movimm(S1, (u64)imm);
                        logic_reg(1, 2, 0, R_AF, dst, S1);
                    }'''
SUFFIX='''}
                if (rd == XZR && !sets)
                    return;'''
TOKEN=re.compile(r'''(?P<literal>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(?P<comment>/\*[\s\S]*?\*/|//[^\n]*)|(?P<space>\s+)|(?P<token>0[xX][0-9a-fA-F]+[uUlL]*|[0-9]+[uUlL]*|[A-Za-z_]\w*|==|!=|>=|<=|&&|\|\||<<|>>|->|.)''')
NAMES=('x86_decode.c','tr_internal.h','include/engine_abi.h','include/translator.h')
RENAME=('movimm','logimm','addr_reg','addr_prepare','ldst','gpr_read','gpr_write','emit_parity','emit_int')


def tokens(text):
    return [(m.group(),m.start(),m.end()) for m in TOKEN.finditer(text)
            if m.lastgroup not in ('comment','space')]


def reconstruct_baseline(source):
    spans=tokens(source);values=[row[0] for row in spans]
    before=[row[0] for row in tokens(PREFIX)];body=[row[0] for row in tokens(CANDIDATE)];after=[row[0] for row in tokens(SUFFIX)]
    needle=before+body+after
    matches=[i for i in range(len(values)-len(needle)+1) if values[i:i+len(needle)]==needle]
    if len(matches)!=1:
        raise ValueError('expected exactly one reviewed candidate immediate-AF guard; refuse constructing baseline')
    start=spans[matches[0]+len(before)][1];end=spans[matches[0]+len(before)+len(body)-1][2]
    return source[:start]+BASELINE+source[end:]


def prepare(engine,work):
    src=engine/'Sources/CEngine';candidate=(src/'emit_int.c').read_text();baseline=reconstruct_baseline(candidate)
    proof=Path(__file__).with_name('af-immediate-flags.c').read_bytes()
    inputs={name:(src/name).read_bytes() for name in NAMES}
    work.mkdir(exist_ok=False);target=work/'input';target.mkdir();(target/'include').mkdir()
    for name,data in inputs.items():(target/name).write_bytes(data)
    (target/'emit_int-baseline.c').write_text(baseline)
    (target/'emit_int-candidate.c').write_text(candidate)
    (work/'af-immediate-flags.c').write_bytes(proof)
    record={'scope':'data-only production decoder/emitter proof; only AF block restored in baseline; no generated ARM execution',
            'candidate_sha256':hashlib.sha256(candidate.encode()).hexdigest(),
            'baseline_sha256':hashlib.sha256(baseline.encode()).hexdigest(),
            'proof_sha256':hashlib.sha256(proof).hexdigest(),
            'production_inputs':{name:hashlib.sha256(data).hexdigest() for name,data in inputs.items()}}
    (work/'input-receipt.json').write_text(json.dumps(record,indent=2)+'\n')
    # The SDK is discovered by the user's shell, never hardcoded or executed here.
    print('lightclick_af_sdk="$(xcrun --sdk macosx --show-sdk-path)"')
    flags=['-O2','-std=c11','-Wall','-Wextra','-Werror','-I'+str(target),'-I'+str(target/'include')]
    def command(args):print('nice -n 10 xcrun --sdk macosx clang -isysroot "$lightclick_af_sdk" '+shlex.join(args))
    for variant in ('baseline','candidate'):
        command(flags+['-D'+name+'='+variant+'_'+name for name in RENAME]+['-c',str(target/('emit_int-'+variant+'.c')),'-o',str(work/(variant+'.o'))])
    command(flags+['-c',str(target/'x86_decode.c'),'-o',str(work/'decoder.o')])
    command(flags+[str(work/'af-immediate-flags.c'),str(work/'baseline.o'),str(work/'candidate.o'),str(work/'decoder.o'),'-o',str(work/'af-proof')])
    print(shlex.join(['nice','-n','10',str(work/'af-proof')])+' > '+shlex.quote(str(work/'proof-result.json')))
    print(shlex.join(['python3',str(Path(__file__)),'check',str(work/'proof-result.json')]))
    return record


def check_result(path):
    raw=path.read_bytes()
    if not 0<len(raw)<=16384:raise ValueError('proof result outside bound')
    value=json.loads(raw)
    expected={'result':'PASS_DATA_ONLY','layout_pairs':2501302,'architectural_state_pairs':76051648,
              'unaffected_controls':131408,'symbolic_memory_CMP_pairs':8192,
              'negative_controls_detected':3,'generated_ARM_executed':False,'guest_or_HV_or_codec_executed':False}
    if not isinstance(value,dict) or any(type(value.get(k)) is not type(v) or value[k]!=v for k,v in expected.items()):
        raise ValueError('proof failed or qualified coverage changed')
    if not all(type(value.get(k)) is int and value[k]>0 for k in
            ('aggregate_words_saved_across_coverage','raw_AF_nonobservable_difference_cases','S1_scratch_difference_cases')):
        raise ValueError('proof did not exercise code-size, raw-AF and scratch differences')
    return {'result':'PASS_DATA_ONLY_CHECKED','terminal_result_sha256':hashlib.sha256(raw).hexdigest(),
            'scope':'array-data equivalence and independent AF truth only; no runtime speed or fault-integration claim'}


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='mode',required=True)
    make=sub.add_parser('prepare');make.add_argument('--engine',type=Path,required=True);make.add_argument('--work',type=Path,required=True)
    verify=sub.add_parser('check');verify.add_argument('result',type=Path)
    args=parser.parse_args()
    if args.mode=='prepare':prepare(args.engine.resolve(),args.work.resolve())
    else:print(json.dumps(check_result(args.result),indent=2))

if __name__=='__main__':main()
