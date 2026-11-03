#!/usr/bin/env python3
"""Finite read-only Leopard kernel survey through the visible app.

Offsets below are independently disassembled from this DVD's i386 kernel:
proc_iterate, proc_pid, proc_ppid, proc_name_address. Not a production ABI or
an arbitrary guest-memory dumper. No process arguments or user buffers read.
The independently symbol/disassembly-qualified 9L31a kernel is admitted ONLY
for --panic-state; process/thread and message-ring layouts remain 9J3050-only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('diagnostics',type=Path)
p.add_argument('--kernel',type=Path,required=True,help='The measured thin i386 kernel (9L31a: panic metadata only)')
p.add_argument('--boot-log',action='store_true',help='Read only this pinned kernel\'s bounded native message ring')
p.add_argument('--panic-state',action='store_true',help='Read pinned kernel panic metadata and at most 512 bytes of its format string')
p.add_argument('--panic-stack-cpu',type=int,choices=range(4),help='With --panic-state, read bounded kernel return PCs for one stopped native panic CPU')
p.add_argument('--active-stack-cpu',type=int,choices=range(4),help='Read one native kernel CPU PC, instruction window and bounded return PCs')
p.add_argument('--kext-address',type=lambda text:int(text,0),action='append',default=[],help='Identify the loaded Leopard kext containing a measured kernel PC')
p.add_argument('--stack-pid',type=int,help='Read bounded kernel return addresses for one named process; no user stacks/arguments')
a=p.parse_args()
if a.panic_stack_cpu is not None and not a.panic_state:p.error('--panic-stack-cpu requires --panic-state')
kernel=a.kernel.read_bytes()
kernel_hash=hashlib.sha256(kernel).hexdigest()
if kernel_hash=='05fd5bf7ad410473d3590e4748ce228bff1297b6b3d182eb20b4c9a3e6adf766':
    if not a.panic_state or a.boot_log or a.stack_pid is not None or a.active_stack_cpu is not None or a.kext_address:
        p.error('9L31a is qualified for bounded native panic metadata only')
elif kernel_hash!='905f1d5e3e12f34f3e1c1a8d7c587ca066495841071c1b7f6f03f815e439ee19':
    p.error('Unqualified kernel layout')
control=Path(__file__).resolve().parents[1]/'scripts/app-guest-command.py'
def command(*args):
    result=subprocess.run([sys.executable,str(control),str(a.diagnostics),*map(str,args)],capture_output=True,text=True,check=True)
    reply=json.loads(result.stdout)
    if 'Error' in reply:raise RuntimeError(reply['Error'])
    return reply['Report']
all_registers=command('--inspect','registers')
registers=all_registers.split('CPU#1')[0]
if 'CPL=0' not in registers or 'CS32' not in registers:raise RuntimeError('CPU0 is not in the measured 32-bit kernel')
root=int(re.search(r'CR3=([0-9a-fA-F]+)',registers)[1],16)&~4095
cache={}
def physical(address,size):
    if size<1 or size>512:raise ValueError('Bounded kernel read required')
    report=command('--physical-memory',hex(address),(size+7)//8)
    values=re.findall(r'0x([0-9a-fA-F]{16})',report)
    data=b''.join(struct.pack('<Q',int(v,16)) for v in values)
    if len(data)<size:raise RuntimeError('Incomplete physical read')
    return data[:size]
def translate(address):
    table=root
    for shift in (39,30,21,12):
        entry_address=table+((address>>shift)&511)*8
        if entry_address not in cache:cache[entry_address]=struct.unpack('<Q',physical(entry_address,8))[0]
        value=cache[entry_address]
        if not value&1:raise RuntimeError('Kernel page is not mapped')
        if shift in (30,21) and value&128:
            return (value&0x000ffffffffff000&~((1<<shift)-1))+(address&((1<<shift)-1))
        table=value&0x000ffffffffff000
    return table+(address&4095)
def virtual(address,size):
    if address<0x100000 or address+size>0x80000000:raise RuntimeError('Outside the measured kernel range')
    data=b''
    while size:
        count=min(size,4096-(address&4095),512)
        data+=physical(translate(address),count);address+=count;size-=count
    return data
if virtual(0x111000,84)!=kernel[:84]:raise RuntimeError('Kernel mapping is not the expected native Mach-O')
if a.kext_address:
    # Native 9J3050 kmod_lookupidbyaddress_locked: _kmod at 0x542678,
    # next at 0, address at 0x94, size at 0x98. Only fixed metadata is read.
    missing=set(a.kext_address);seen=set()
    node=struct.unpack('<I',virtual(0x542678,4))[0]
    for _ in range(128):
        if not node or not missing:break
        if node in seen:raise RuntimeError('Kext-list cycle or concurrent mutation')
        seen.add(node);data=virtual(node,0x9c)
        start,size=struct.unpack_from('<II',data,0x94)
        for address in tuple(missing):
            if start<=address<start+size:
                print(json.dumps({'KernelPC':hex(address),'KextBase':hex(start),'KextSize':hex(size),
                                  'Kext':data[12:76].split(b'\0')[0].decode('ascii',errors='replace')}),flush=True)
                missing.remove(address)
        node=struct.unpack_from('<I',data)[0]
    for address in sorted(missing):print(json.dumps({'KernelPC':hex(address),'Kext':None}),flush=True)
    raise SystemExit(0)
def current_cpu_stack(number,include_instructions=False):
    cpu=all_registers.split('CPU#'+str(number)+'\r\n')
    if len(cpu)!=2:raise RuntimeError('Requested native CPU report unavailable')
    cpu=cpu[1].split('CPU#')[0]
    if 'CPL=0' not in cpu or 'CS32' not in cpu:raise RuntimeError('Requested CPU is not in the native kernel')
    if include_instructions:
        pc=int(re.search(r'EIP=([0-9a-fA-F]+)',cpu)[1],16)
        print(json.dumps({'CPU':number,'KernelPC':hex(pc),'Instructions':virtual(pc,16).hex()}),flush=True)
    frame=int(re.search(r'EBP=([0-9a-fA-F]+)',cpu)[1],16)
    stack=int(re.search(r'ESP=([0-9a-fA-F]+)',cpu)[1],16)
    # Both kernels' machine_stack_attach establishes a 16KiB stack. Cap
    # reads to increasing frame records above ESP and print return PCs only.
    for _ in range(16):
        if frame&3 or not stack<=frame<=stack+0x4000-8:break
        following,pc=struct.unpack('<II',virtual(frame,8))
        print(json.dumps({'CPU':number,'KernelReturn':hex(pc)}),flush=True)
        if following<=frame:break
        frame=following
if a.active_stack_cpu is not None:
    current_cpu_stack(a.active_stack_cpu,include_instructions=True)
    raise SystemExit(0)
if a.panic_state:
    # Native Mach-O symbols, not another build's KDK offsets. Read only the
    # panic format string, never arbitrary formatted arguments/user buffers.
    # 9L31a's own nm/disassembly independently confirms these same data symbols
    # and panic's accesses. No 9J process/thread layout is inferred for 9L31a.
    fields={name:struct.unpack('<I',virtual(address,4))[0] for name,address in
        [('Format',0x4dd288),('Caller',0x545a6c),('CPU',0x545aa8),('Wait',0x545aac)]}
    print(json.dumps({name:hex(value) for name,value in fields.items()}),flush=True)
    if kernel_hash=='05fd5bf7ad410473d3590e4748ce228bff1297b6b3d182eb20b4c9a3e6adf766':
        # Actual9L31a ml_init_lock_timeout and its own native Mach-O symbols.
        absolute,tsc=struct.unpack('<II',virtual(0x53dd0c,8))
        print(json.dumps({'LockTimeOutAbsolute':absolute,'LockTimeOutTSC':tsc}),flush=True)
    if fields['Format']:
        print(virtual(fields['Format'],512).split(b'\0')[0].decode('utf-8',errors='replace'))
        capacity,end=struct.unpack('<II',virtual(0x4dd274,8))
        used=end-0x53f000
        print(json.dumps({'PanicLogCapacity':capacity,'PanicLogUsed':used,'PanicLogEnd':hex(end)}),flush=True)
        if not 0<=used<=capacity<=65536:raise RuntimeError('Unqualified native panic log bounds')
        print(virtual(0x53f000,min(used,8192)).decode('utf-8',errors='replace') if used else '<empty native panic log>')
        if a.panic_stack_cpu is not None:current_cpu_stack(a.panic_stack_cpu)
    raise SystemExit(0)
if a.boot_log:
    # Exact kernel's log_putc_locked disassembly: magic0, size4, write8,
    # read12, buffer16. Independently agrees with XNU1228's msgbuf.h.
    pointer=struct.unpack('<I',virtual(0x559d08,4))[0]
    magic,size,write,read,buffer=struct.unpack('<IIIII',virtual(pointer,20))
    if magic!=0x63061 or not 4096<=size<=65536 or write>=size or read>=size:
        raise RuntimeError('Unqualified native message ring')
    data=virtual(buffer,size)
    print((data[write:]+data[:write]).strip(b'\0').decode('utf-8',errors='replace'))
    raise SystemExit(0)
proc=struct.unpack('<I',virtual(0x54a00c,4))[0];seen=set()
def stacks(proc,pid,name):
    # Native get_bsdtask_info, task_threads, get_threadtask and
    # machine_stack_attach/detach disassembly independently fixes these
    # offsets. Check both back-pointers, never follow a guessed layout.
    task=struct.unpack('<I',virtual(proc+12,4))[0]
    if struct.unpack('<I',virtual(task+0x1cc,4))[0]!=proc:raise RuntimeError('Task/proc identity mismatch')
    thread=struct.unpack('<I',virtual(task+0x28,4))[0]
    count=struct.unpack('<I',virtual(task+0x30,4))[0]
    if not 1<=count<=32:raise RuntimeError('Unexpected bounded thread count')
    for index in range(min(count,8)):
        if struct.unpack('<I',virtual(thread+0x2bc,4))[0]!=task:raise RuntimeError('Thread/task identity mismatch')
        stack=struct.unpack('<I',virtual(thread+0x8c,4))[0]
        print(json.dumps({'PID':pid,'Name':name,'Thread':index,'KernelStack':hex(stack)}),flush=True)
        if stack:
            # Native x86_kernel_state32 occupies the final28 bytes of the
            # 16KiB stack; print PCs only, not saved general registers.
            saved=struct.unpack('<7I',virtual(stack+0x4000-28,28))
            print(json.dumps({'KernelPC':hex(saved[5])}),flush=True)
            frame=saved[2]
            for _ in range(16):
                if frame&3 or not stack<=frame<=stack+0x4000-8:break
                following,pc=struct.unpack('<II',virtual(frame,8))
                print(json.dumps({'KernelReturn':hex(pc)}),flush=True)
                if following<=frame:break
                frame=following
        thread=struct.unpack('<I',virtual(thread+0x210,4))[0]
for _ in range(128):
    if not proc:break
    if proc in seen:raise RuntimeError('Process-list cycle or concurrent mutation')
    seen.add(proc);data=virtual(proc,0x1b1)
    following=struct.unpack_from('<I',data)[0]
    pid=struct.unpack_from('<I',data,8)[0];ppid=struct.unpack_from('<I',data,0x14)[0]
    name=data[0x1a0:0x1b1].split(b'\0')[0].decode('ascii',errors='replace')
    if a.stack_pid is not None:
        if pid==a.stack_pid:
            stacks(proc,pid,name);raise SystemExit(0)
    else:print(json.dumps({'PID':pid,'PPID':ppid,'State':data[0x28],'Name':name}),flush=True)
    proc=following
else:raise RuntimeError('Reached finite process cap')
if a.stack_pid is not None:raise RuntimeError('Requested process is no longer present')
