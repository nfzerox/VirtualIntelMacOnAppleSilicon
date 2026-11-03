#!/usr/bin/env python3
"""Type bounded diagnostic ASCII from stdin via the visible app's input owner.

Focus a throwaway guest Terminal first. Not unattended-install automation and
not for passwords. No clipboard, guest tools, host event tap or second QMP.
"""
import json
from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv)!=2:
        raise SystemExit('Usage: app-guest-type.py APP_DIAGNOSTICS_DIRECTORY < commands.txt')
    text=sys.stdin.read(4097)
    if len(text)>4096 or any(ord(c)>126 or (ord(c)<32 and c not in '\n\t') for c in text):
        raise SystemExit('Choose at most 4096 diagnostic ASCII characters.')
    plain={' ':'spc','\n':'ret','\t':'tab','/':'slash','.':'dot','-':'minus',';':'semicolon',
           "'":'apostrophe','=':'equal',',':'comma','[':'bracket_left',']':'bracket_right',
           '\\':'backslash','`':'grave_accent'}
    shifted=dict(zip('!@#$%^&*()_+{}|:"<>?~','1234567890-=[]\\;\',./`'))
    plan=[]
    def key(code,down):return {'type':'key','data':{'down':down,'key':{'type':'qcode','data':code}}}
    for c in text:
        shift=c.isupper() or c in shifted
        base=c.lower() if c.isupper() else shifted.get(c,c)
        code=plain.get(base,base)
        if not (base.isalnum() or base in plain):raise SystemExit('Unsupported ASCII key: '+repr(c))
        events=([key('shift',True)] if shift else [])+[key(code,True),key(code,False)]+([key('shift',False)] if shift else [])
        plan.append(events)
    owner=Path(__file__).resolve().parents[1]/'scripts/app-guest-command.py'
    batch=[]
    def send():
        if batch:subprocess.run([sys.executable,str(owner),sys.argv[1],'--input-json',json.dumps(batch)],check=True,stdout=subprocess.DEVNULL)
    for events in plan:
        # Stay below the USB keyboard report FIFO's capacity. The app request
        # completion/poll handoff naturally spaces batches; don't hold keys
        # while waiting for another request or use guest repeat to type text.
        if len(batch)+len(events)>12:send();batch=[]
        batch+=events
    send()
    print(f'Queued {len(text)} diagnostic characters.')


if __name__=='__main__':main()
