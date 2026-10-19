#!/usr/bin/env python3
"""Exercise seedless Catalina staging in temporary roots; signing is mocked."""
import os
from pathlib import Path
import plistlib
import runpy
import subprocess
import tempfile
import sys

repo = Path(__file__).resolve().parents[1]
arguments = sys.argv[:]
sys.argv = sys.argv[:1]
fixture = runpy.run_path(str(repo/'scripts/test-guest-patches.py'))['fixture']
sys.argv = arguments
with tempfile.TemporaryDirectory(prefix='vim-catalina-stage-') as temporary:
    base = Path(temporary)
    guest, payload, mocks = (base/n for n in ('guest', 'payload', 'bin'))
    mocks.mkdir()
    signer = mocks/'codesign'
    signer.write_text('#!/bin/sh\nexit 0\n'); signer.chmod(0o755)
    core = 'System/Library/Frameworks/CoreDisplay.framework/Versions/A/CoreDisplay'
    compiler = 'System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/3902/MTLCompiler'
    service = 'System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc/Contents/MacOS/MTLCompilerService'
    files = {
        core: fixture(512),
        compiler: fixture(512)+b'\0AppleIntelHD4000GraphicsMTLDriver.bundle/Contents/MacOS/libMTLIntelCompilerPlugin.dylib\0/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib\0',
        service: fixture(512)+b'\0/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/3902/MTLCompiler\0',
        str(Path(compiler).parent/'Resources/current.txt'): b'current Catalina compiler resources',
        'System/Library/CoreServices/SystemVersion.plist': plistlib.dumps({'ProductVersion': '10.15.6', 'ProductBuildVersion': '19Z9999'}),
    }
    for relative, data in files.items():
        path = guest/relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
    environment = dict(os.environ, PATH=str(mocks)+':'+os.environ['PATH'], VIM_TEST_SIGNER=str(signer))
    payload.mkdir()
    command=([sys.argv[1],str(guest),str(payload),str(repo/'prebuilt/tools')] if len(sys.argv)>1 else
             ['/bin/zsh', str(repo/'scripts/pvg-stage-guest'), str(guest), str(payload)])
    subprocess.run(command, env=environment, check=True)
    assert all((guest/p).read_bytes() == data for p, data in files.items())
    rebuilt = payload/compiler.replace('/3902/', '/3903/')
    assert b'AppleParavirtGPUMetal.bundle' in rebuilt.read_bytes()
    assert (rebuilt.parent/'Resources/current.txt').read_bytes() == b'current Catalina compiler resources'
    assert b'/Versions/3903/MTLCompiler' in (payload/service).read_bytes()
    assert (payload/core).read_bytes() != files[core]
    if len(sys.argv)>1:
        receipt_path=payload/'.VirtualIntelMacPreparation.plist'
        receipt=plistlib.loads(receipt_path.read_bytes())
        assert receipt['Prepared'] and not receipt['Installed']
        assert receipt['ProductBuildVersion']=='19Z9999'
        # An unsuccessful re-preparation must invalidate the old success record.
        (guest/core).write_bytes(b'unknown native image')
        failed=subprocess.run(command,env=environment,capture_output=True)
        assert failed.returncode != 0
        assert not receipt_path.exists()
        assert list(base.glob('PreviousPreparation-*'))
print('PASS Catalina seedless staging: current native code/resources, source preservation')
