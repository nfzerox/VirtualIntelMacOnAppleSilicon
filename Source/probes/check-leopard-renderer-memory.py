#!/usr/bin/env python3
"""Compile the production renderer-memory selection without a period SDK.

Usage: python3 check-leopard-renderer-memory.py [PassthroughOpenGL directory]
This tests actual source selection and units, not a duplicated conversion.
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

source = (pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else
          pathlib.Path(__file__).resolve().parents[1] / 'PassthroughOpenGL')
text = (source / 'passthrough-opengl-gli.c').read_text()
start = text.index('#if VIM_GLI_DISPATCH_COUNT == 757',
                   text.index('VIMGLITrace("renderer memory'))
selection = text[start:text.index('    if(software)', start)]
compiler = shutil.which('clang') or shutil.which('cc')
if not compiler:
    raise SystemExit('A host C compiler is required')

fixture = r'''
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "renderer-memory.h"
#undef __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#define __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ MINIMUM
int main(void) {
    const int budgets[]={-1,0,1,15,16,2047,2048,4096,24576,INT_MAX};
    for(unsigned i=0;i<sizeof(budgets)/sizeof(budgets[0]);++i) {
        int mb=budgets[i];
        struct {int video_mb,texture_mb;} properties={mb,mb};
        struct {uint32_t data[32];} record={0},*r=&record;
SELECTION
        uint32_t expected;
        if(EXPECT_BYTES) {
            uint64_t bytes=mb>0?(uint64_t)(unsigned)mb*1048576:0;
            expected=bytes>INT32_MAX?INT32_MAX:(uint32_t)bytes;
        } else if(EXPECT_CLAMP) expected=mb<=0?0:mb>2047?2047:(uint32_t)mb;
        else expected=(uint32_t)mb;
        assert(record.data[VIDEO_SLOT]==expected);
        assert(record.data[TEXTURE_SLOT]==expected);
    }
    return 0;
}
'''.replace('SELECTION', selection)
with tempfile.TemporaryDirectory(prefix='vim-renderer-memory-') as temp:
    path = pathlib.Path(temp)
    (path / 'probe.c').write_text(fixture)
    cases = [('Tiger', 1040, 759, 1, 0, 11, 12),
             ('Leopard', 1050, 757, 1, 0, 11, 12),
             ('Snow Leopard', 1060, 764, 0, 1, 17, 19),
             ('Mavericks', 1090, 974, 0, 0, 17, 19),
             ('Mojave', 101400, 975, 0, 0, 17, 19)]
    for name, minimum, dispatch, byte_units, clamp, video, texture in cases:
        defines = {'MINIMUM': minimum, 'VIM_GLI_DISPATCH_COUNT': dispatch,
                   'EXPECT_BYTES': byte_units, 'EXPECT_CLAMP': clamp,
                   'VIDEO_SLOT': video, 'TEXTURE_SLOT': texture}
        subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(source), str(path / 'probe.c'),
                        '-o', str(path / 'probe')] +
                       ['-D%s=%d' % item for item in defines.items()], check=True)
        subprocess.run([str(path / 'probe')], check=True)
        print('PASS %s renderer memory units, saturation, and slots' % name)
