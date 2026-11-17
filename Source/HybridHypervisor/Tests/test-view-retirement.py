#!/usr/bin/env python3
"""Compile the actual monitor view selectors with observable retirement edges.

Native deterministic interleaving test, not an ARM-MMU/SMP qualification.
The pre-fix composed call is a negative control for both retiring the active
root before TTBR replacement and the temporary kernel-pin rotation gap.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'Monitor/monitor.c').read_text()


def function(name):
    found = re.search(r'(?m)^static [^;{\n]+\b' + name + r'\([^;{]*\)\s*\{', source)
    assert found, name
    end, depth = found.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[found.start():end]
    # Barriers are observable control boundaries here, not hardware execution.
    body = re.sub(r'__asm__ volatile\("isb"(?: : : : "memory")?\);', 'barrier();', body)
    if name == 'view_find':
        # Exercise the adversarial scheduler at each actual rotation store.
        for statement in ('me->pin[2] = *pin;', '*pin = before;'):
            assert statement in body
            body = body.replace(statement, '{ ' + statement + ' retirement_edge(); }')
    return body


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
typedef uint32_t u32;
#define PRIV_VA 0xF00000000000ULL
#define R_RS 26
#define R_HASH 19
#define STOP_UNSUPPORTED 1
typedef struct {
    u64 cr3;
    u32 cpl, valid, sees_shared, pad_shared;
    u64 root0, root1, ttbr0, ttbr1, hash_va, last_used;
    u32 keeper, pad;
} View;
static View views[8];
static struct { u32 pin[3]; u64 rs_saved[8]; u32 lock_site; } state, *me=&state;
static struct { u32 view; u64 x[31], rs_top; } context, *c=&context;
static u64 roots[8][512], hole_entry_priv=0x1003;
static u32 depth, acquisitions, hardware_view, target_view;
static bool retired_active, retired_target;
static bool pinned(u32 i) {
    return me->pin[0]==i+1 || me->pin[1]==i+1 || me->pin[2]==i+1;
}
static void retirement_edge(void) {
    if (depth) return;  // The real reclaimer takes this same monitor lock.
    if (!pinned(hardware_view)) retired_active=true;
    if (!pinned(target_view)) retired_target=true;
}
static void mon_lock(void) { if (!depth++) acquisitions++; }
static void mon_unlock(void) { assert(depth); if (!--depth) retirement_edge(); }
static void barrier(void) {}
static void stop(int reason,u64 a,u64 b,u64 d,u64 e) {
    (void)reason;(void)a;(void)b;(void)d;(void)e; abort();
}
static u64 view_rs_top(u32 i) { return 0x100000+i*0x10000; }
static void view_rs_prepare(u32 i) { me->rs_saved[i]=view_rs_top(i); }
static View *view_current(void) { return &views[c->view]; }
static View *view_find_locked(u64 key,u32 mode) {
    assert(depth);
    for(u32 i=0;i<8;i++) if(views[i].cr3==key && views[i].cpl==mode) return &views[i];
    abort();
}
static void write_ttbr(u64 value) {
    retirement_edge();
    for(u32 i=0;i<8;i++) if(views[i].ttbr0==value) hardware_view=i;
}
#define WSR(reg,value) write_ttbr(value)
'''

checks = r'''
static void setup(u32 current,u32 mode,u32 target) {
    memset(&state,0,sizeof state); memset(&context,0,sizeof context);
    depth=acquisitions=0;retired_active=retired_target=false;
    hardware_view=current;target_view=target;c->view=current;
    for(u32 i=0;i<8;i++) {
        memset(&views[i],0,sizeof views[i]);
        roots[i][(PRIV_VA>>39)&511]=hole_entry_priv;
        views[i].cr3=0x1000*(i+1);views[i].valid=1;
        views[i].root0=(u64)roots[i];views[i].root1=(u64)roots[i];
        views[i].ttbr0=0x10000*(i+1);views[i].ttbr1=~views[i].ttbr0;
        views[i].cpl=mode;
    }
}
static void check_user_switch(bool old) {
    setup(1,3,2);me->pin[0]=1;me->pin[1]=2;
    if(old) view_activate(view_find(views[2].cr3,3));
    else view_switch(views[2].cr3,3);
    assert(!depth && hardware_view==2);
    assert(old ? retired_active : !retired_active && !retired_target);
}
static void check_kernel_rotation(bool old) {
    setup(0,0,1);me->pin[0]=1;me->pin[2]=2;
    if(old) view_activate(view_find(views[1].cr3,0));
    else view_switch(views[1].cr3,0);
    assert(!depth && hardware_view==1);
    assert(old ? retired_target : !retired_target && !retired_active);
}
static void check_cached_cpl(void) {
    setup(0,0,1);views[1].cpl=3;me->pin[0]=1;me->pin[1]=2;
    view_switch(views[1].cr3,3);
    assert(!depth && !acquisitions && hardware_view==1);
    assert(!retired_active && !retired_target);
    target_view=0;view_switch(views[0].cr3,0);
    assert(!depth && !acquisitions && hardware_view==0);
}
static void check_compatibility(void) {
    setup(0,0,1);views[1].cpl=4;me->pin[0]=1;me->pin[1]=3;
    view_switch(views[1].cr3,4);
    assert(!depth && acquisitions==1 && hardware_view==1);
    assert(!retired_active && !retired_target);
}
int main(void) {
    check_user_switch(true);check_kernel_rotation(true);
    puts("PASS negative controls reproduce active-root retirement and kernel-pin gap");
    for(int n=0;n<10000;n++) {
        check_user_switch(false);check_kernel_rotation(false);
        check_cached_cpl();check_compatibility();
    }
    puts("PASS actual selectors: guarded user/compatibility switches and kernel rotation; cached CPL path stays lock-free");
    return 0;
}
'''

data = prefix + '\n\n'.join(function(n) for n in ('view_find','view_activate','view_switch')) + checks
with tempfile.TemporaryDirectory(prefix='vim-view-retirement-', dir=os.environ.get('TMPDIR')) as folder:
    path=Path(folder)/'fixture.c'
    path.write_text(data)
    executable=Path(folder)/'fixture'
    subprocess.run(['xcrun','clang','-O2','-std=c11','-Wall','-Wextra','-Werror',str(path),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
