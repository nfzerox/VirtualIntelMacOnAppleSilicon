#!/usr/bin/env python3
"""Run the production dirty-range batch wrapper with finite bitmap/TLB mocks."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'vendor/qemu/system/physmem.c').read_text()
start=source.index('uint64_t physical_memory_test_and_clear_dirty_ranges(')
end=source.index('static void physical_memory_clear_dirty_range(',start)
wrapper=source[start:end]
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t ram_addr_t;
typedef struct {ram_addr_t offset,used_length;} RAMBlock;
typedef struct {ram_addr_t start,length;} RAMDirtyRange;
static RAMBlock block={0x100000,16*4096};
static unsigned char dirty[16];
static unsigned calls,scanned,notifications;
static ram_addr_t notified_start,notified_length;
#define RCU_READ_LOCK_GUARD() ((void)0)
static RAMBlock *qemu_get_ram_block(ram_addr_t start) {
    assert(start>=block.offset&&start<block.offset+block.used_length);
    return &block;
}
static uint64_t physical_memory_test_and_clear_dirty_internal(ram_addr_t start,
    ram_addr_t length,unsigned client,unsigned long *bmap,bool notify) {
    assert(client==2&&!bmap&&!notify);
    assert(!(start%4096)&&!(length%4096));
    calls++;
    unsigned first=(unsigned)((start-block.offset)/4096),n=(unsigned)(length/4096);
    unsigned total=0;
    for(unsigned i=first;i<first+n;i++){total+=dirty[i];dirty[i]=0;scanned++;}
    return total;
}
static void physical_memory_dirty_bits_cleared(ram_addr_t start,ram_addr_t length) {
    notifications++;notified_start=start;notified_length=length;
}
'''
checks=r'''
int main(void) {
    const RAMDirtyRange ranges[]={{0x101000,4096},{0x10c000,8192}};
    for(unsigned i=0;i<16;i++)dirty[i]=1;
    assert(physical_memory_test_and_clear_dirty_ranges(NULL,0,2)==0);
    assert(!calls&&!notifications);
    assert(physical_memory_test_and_clear_dirty_ranges(ranges,2,2)==3);
    assert(calls==2&&scanned==3&&notifications==1);
    assert(notified_start==0x101000&&notified_length==13*4096);
    for(unsigned i=0;i<16;i++)assert(dirty[i]==!(i==1||i==12||i==13));
    assert(physical_memory_test_and_clear_dirty_ranges(ranges,2,2)==0);
    assert(notifications==1);
    puts("PASS production batch: exact dirty ranges, one enclosing TLB notification, empty/clean no-op");
}
'''
with tempfile.TemporaryDirectory(prefix='vim-dirty-ranges-') as folder:
    path=Path(folder)/'check.c';path.write_text(fixture+wrapper+checks)
    binary=path.with_suffix('')
    subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
