#!/usr/bin/env python3
"""Prepare/compare production SSE emission as nonexecutable integer data.

This script never compiles or runs binaries. Root executes the printed compile/
fixture commands explicitly. Copies only the named Engine sources/headers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import shlex

BASELINE='''static Addr mem16(const Insn *in, bool check) {
    int r = addr_reg(in);
    if (check)
        align16(r);
    Addr a = {.base = r};
    return a;
}'''
CANDIDATE='''static Addr mem16(const Insn *in, bool check) {
    int r = addr_reg(in);
    bool aligned_constant = in->riprel && !in->seg && !((in->next + (u64)in->disp) & 15);
    if (check && !aligned_constant)
        align16(r);
    Addr a = {.base = r};
    return a;
}'''

def tokens(text):
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    return ''.join(text.split())

def prepare(engine,work):
    src=engine/'Sources/CEngine'
    source=(src/'emit_sse.c').read_text()
    match=re.search(r'static Addr mem16\(const Insn \*in, bool check\) \{.*?\n\}',source,re.S)
    if match is None or tokens(match.group())!=tokens(CANDIDATE):
        raise ValueError('candidate mem16 differs from independently reviewed minimal guard; refuse constructing baseline')
    baseline=source[:match.start()]+BASELINE+source[match.end():]
    work.mkdir(exist_ok=False)
    target=work/'input';target.mkdir()
    for name in ('x86_decode.c','emit_int.c','tr_internal.h'):
        shutil.copy2(src/name,target/name)
    shutil.copytree(src/'include',target/'include',symlinks=True)
    (target/'emit_sse-candidate.c').write_text(source)
    (target/'emit_sse-baseline.c').write_text(baseline)
    shutil.copy2(Path(__file__).with_name('sse-aligned-constants.c'),work/'emit-cases.c')
    record={'candidate_sha256':hashlib.sha256(source.encode()).hexdigest(),
            'baseline_sha256':hashlib.sha256(baseline.encode()).hexdigest(),
            'scope':'only mem16 guard removed in baseline; all other production emitter bytes identical'}
    (work/'input-receipt.json').write_text(json.dumps(record,indent=2)+'\n')
    for variant in ('baseline','candidate'):
        command=['nice','-n','10','clang','-O2','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                 '-I'+str(target),'-I'+str(target/'include'),str(work/'emit-cases.c'),
                 str(target/'x86_decode.c'),str(target/'emit_int.c'),str(target/('emit_sse-'+variant+'.c')),
                 '-o',str(work/('emit-'+variant))]
        print(shlex.join(command))
        print(shlex.join(['nice','-n','10',str(work/('emit-'+variant))])+' > '+shlex.quote(str(work/(variant+'.json'))))
    print(shlex.join(['python3',str(Path(__file__)),'compare',str(work/'baseline.json'),str(work/'candidate.json')]))

def positions(words,guard):
    return [i for i in range(len(words)-2) if words[i:i+3]==guard]

def neon(word):
    # SIMD/FP data-processing major classes, plus SIMD load/store (V=1).
    return (word&0x1e000000) in (0x0e000000,0x1e000000) or memory(word)

def memory(word):
    return word&0x0e000000==0x0c000000 and bool(word&(1<<26))

def compare(old,new):
    assert isinstance(old,list) and isinstance(new,list) and len(old)==len(new)>0
    checks=0;elisions=0
    for a,b in zip(old,new):
        metadata=[k for k in a if k!='words']
        assert set(a)==set(b) and all(a[k]==b[k] for k in metadata),(a['name'],'metadata mismatch')
        for row in (a,b):
            assert len(row['words'])<65536 and all(type(w) is int and 0<=w<=0xffffffff for w in row['words'])
        before,after=a['words'],b['words'];guard=a['guard'];hits=positions(before,guard)
        assert len(guard)==3 and len(hits)==a['expected_old_guards'],(a['name'],'old guard count',hits)
        if a['expected_elision']:
            assert len(hits)==1
            cut=hits[0]
            assert after==before[:cut]+before[cut+3:],(a['name'],'more than exact3 guard words changed')
            assert positions(after,guard)==[],(a['name'],'eligible guard retained')
            elisions+=1
        else:
            assert after==before,(a['name'],'ineligible/checkfalse emission changed')
        first_old=next((w for w in before if neon(w)),None)
        first_new=next((w for w in after if neon(w)),None)
        assert first_old is not None and first_new==first_old,(a['name'],'first NEON touch changed')
        assert [w for w in before if memory(w)]==[w for w in after if memory(w)],(a['name'],'memory word sequence changed')
        if a['expected_old_guards']:
            assert len([w for w in before if memory(w)])>=1,(a['name'],'memory form emitted no SIMD access')
        checks+=1
    expected={'cases':148,'elisions':14,'words_removed':42}
    actual={'cases':checks,'elisions':elisions,'words_removed':elisions*3}
    assert actual==expected,(actual,expected)
    return dict(result='PASS_DATA_ONLY_EMISSION_REGRESSION',**actual,
                generated_code_executed=False,hv_used=False,exception_runtime_qualified=False,
                scope='Manual real-decoded cases; exact baseline minus3 words only, first NEON touch/memory words unchanged')

def bounded_json(path):
    with path.open('rb') as stream:data=stream.read(4*1024*1024+1)
    if len(data)>4*1024*1024:raise ValueError('fixture result outside bound')
    return json.loads(data)

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='mode',required=True)
    make=sub.add_parser('prepare');make.add_argument('--engine',type=Path,required=True);make.add_argument('--work',type=Path,required=True)
    diff=sub.add_parser('compare');diff.add_argument('baseline',type=Path);diff.add_argument('candidate',type=Path)
    args=p.parse_args()
    if args.mode=='prepare':prepare(args.engine.resolve(),args.work.resolve())
    else:
        baseline,candidate=bounded_json(args.baseline),bounded_json(args.candidate)
        result=compare(baseline,candidate)
        try:compare(baseline,baseline)
        except AssertionError:result['unoptimized_negative_control_rejected']=True
        else:raise AssertionError('unoptimized negative control incorrectly passed')
        print(json.dumps(result,indent=2))
if __name__=='__main__':main()
