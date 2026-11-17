#!/usr/bin/env python3
"""Exercise real Mojave staging in temporary roots; only signing is mocked."""
import os
import plistlib
import runpy
import subprocess
import tempfile
import struct
import sys
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
fixture = runpy.run_path(str(repo/'scripts/test-mojave-patches.py'))['fixture']
core = 'System/Library/Frameworks/CoreDisplay.framework/Versions/A/CoreDisplay'
sky = 'System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight'
ioaccel = 'System/Library/PrivateFrameworks/IOAccelerator.framework/Versions/A/IOAccelerator'
vpa = 'System/Library/PrivateFrameworks/AppleVPA.framework/Versions/A/Resources/Info.plist'
aperture = 'Library/Preferences/com.apple.Aperture.plist'
compiler = 'System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/3902/MTLCompiler'
service = 'System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc/Contents/MacOS/MTLCompilerService'
compiler_strings = (b'\0AppleIntelHD4000GraphicsMTLDriver.bundle/Contents/MacOS/libMTLIntelCompilerPlugin.dylib\0'
                    b'/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib\0')
service_string = b'\0/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/3902/MTLCompiler\0'
for bad, seedless, passthrough in ((False, False, False), (False, True, False),
                                  (True, False, False), (False, True, True)):
    with tempfile.TemporaryDirectory(prefix='pvg-stage-test-') as folder:
        base = Path(folder); guest = base/'guest with spaces'; payload = base/'payload'; mocks = base/'bin'
        mocks.mkdir(); tool = mocks/'codesign'; tool.write_text('#!/bin/sh\nprintf "%s\\n" "$@" >> "$VIM_SIGN_LOG"\nexit 0\n'); tool.chmod(0o755)
        files = {core: fixture('core', 0x100000000, 0x100, native_metadata=True),
                 sky: fixture('sky', 0x7fff30000000, 0x200, 'unknown' if bad else None, native_metadata=True),
                 compiler: fixture('core', 0)+compiler_strings,
                 service: fixture('core', 0)+service_string,
                 str(Path(compiler).parent/'Resources/current.txt'): b'current guest resource'}
        raw=fixture('core',0)
        files[vpa]=plistlib.dumps({'System Support': {'Mac-fixture': {'jpeg': True, 'forceOfflineRenderer': True}, 'Mac-other': {}}, 'CFBundleVersion': 'native'})
        if not seedless:
            files[aperture]=plistlib.dumps({'UnrelatedPreference': 'preserve me',
                                          'NSViewAllowsRootLayerBacking': False})
        other=struct.pack('<7I',0xfeedface,7,3,6,0,0,0)+b'original i386 client'
        other_offset=(4096+len(raw)+4095)&~4095
        header=struct.pack('>12I',0xcafebabe,2,0x1000007,3,4096,len(raw),12,
                           7,3,other_offset,len(other),12)
        files[ioaccel]=(header.ljust(4096,b'\0')+raw).ljust(other_offset,b'\0')+other
        client=payload/ioaccel;client.parent.mkdir(parents=True);client.write_bytes(raw+b'donor replacement')
        helper=payload/'usr/lib/PVGGLIOKit.dylib'
        helper.parent.mkdir(parents=True,exist_ok=True);helper.write_bytes(b'fixture Surface2 helper')
        # The staging path now preserves the native i386 OpenGL entry too.
        gl='System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL'
        identity=b'/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL\0'
        command_size=(24+len(identity)+3)&~3
        command=(struct.pack('<6I',13,command_size,24,0,0,0)+identity).ljust(command_size,b'\0')
        files[gl]=(struct.pack('<7I',0xfeedface,7,3,6,1,len(command),0)+command).ljust(4096,b'\0')
        donor=payload/gl;donor.parent.mkdir(parents=True);donor.write_bytes(raw)
        cvms='System/Library/Frameworks/OpenGL.framework/Versions/A/Libraries/CVMServer'
        files[cvms]=files[ioaccel]
        donor_cvms=payload/cvms;donor_cvms.parent.mkdir(parents=True);donor_cvms.write_bytes(raw)
        engine='System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/GLEngine'
        files[engine]=files[ioaccel]
        donor_engine=payload/engine;donor_engine.parent.mkdir(parents=True);donor_engine.write_bytes(raw)
        manifest=donor.parent/'Resources/PVGDonorFiles.txt'
        manifest.parent.mkdir(exist_ok=True);manifest.write_text('Versions/A/OpenGL\nVersions/A/Libraries/CVMServer\nVersions/A/Resources/GLEngine.bundle/GLEngine\n')
        optional_engine=raw+b'_VIMGLINotifyCPUSurfaceCopy\0'
        if not bad:
            addon=payload/'GuestHelpers/PassthroughOpenGL';addon.mkdir(parents=True)
            (addon/'GLEngine').write_bytes(optional_engine)
            (addon/'GLEngineSelector').write_bytes(raw+b'_VIMOpenGLBootSelection\0')
            (addon/'libVIMPassthroughOpenGL.dylib').write_bytes(raw)
            driver=addon/'PassthroughOpenGLTransport.kext/Contents/MacOS/PassthroughOpenGLTransport'
            driver.parent.mkdir(parents=True);driver.write_bytes(raw)
        for name, data in files.items():
            path=guest/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        version=guest/'System/Library/CoreServices/SystemVersion.plist'; version.parent.mkdir(parents=True)
        version.write_bytes(plistlib.dumps({'ProductVersion':'10.14.5','ProductBuildVersion':'18Z9999'}))
        # Deliberately stale seeds must never be installed over native binaries.
        for name in (() if seedless else (core, sky, compiler.replace('/3902/','/3903/'))):
            path=payload/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(b'stale reference')
        sign_log=base/'sign.log'
        env=dict(os.environ, PATH=str(mocks)+':'+os.environ['PATH'], VIM_TEST_SIGNER=str(tool),VIM_SIGN_LOG=str(sign_log))
        payload.mkdir(exist_ok=True)
        tools=Path(sys.argv[2]) if len(sys.argv)>2 else repo/'prebuilt/tools'
        command=([sys.argv[1],str(guest),str(payload),str(tools)] if len(sys.argv)>1 else
                 ['/bin/zsh',str(repo/'scripts/pvg-stage-guest'),str(guest),str(payload)])
        if passthrough:
            assert len(sys.argv)>1, 'Optional renderer is implemented by native preparation'
            command.append('--passthrough-opengl')
        result=subprocess.run(command,
                              capture_output=True,text=True,env=env)
        assert (result.returncode != 0) == bad, result.stdout+result.stderr
        if not bad:
            signing=sign_log.read_text().splitlines()
            policy=plistlib.loads(Path(signing[signing.index('--entitlements')+1]).read_bytes())
            assert policy==plistlib.loads((repo/'support/CVMServer.entitlements.plist').read_bytes())
            assert policy=={'jit-codesigning':True,'com.apple.security.cs.allow-jit':True,
                            'com.apple.rootless.storage.cvms':True}
            assert not (payload/aperture).exists()
            assert not (payload/'Library/Preferences/com.apple.iPhoto.plist').exists()
            assert not any('/Library/Preferences/' in target for target in
                           (repo/'support/install-targets-mojave.txt').read_text().splitlines())
            assert b'/usr/lib/PVGGLIOKit.dylib\0' in (payload/sky).read_bytes()
            assert helper.is_file()
            capabilities=plistlib.loads((payload/vpa).read_bytes())
            assert capabilities['System Support']['Mac-fixture']=={'jpeg': False, 'forceOfflineRenderer': True}
            assert capabilities['System Support']['Mac-other']=={}
            assert capabilities['CFBundleVersion']=='native'
        assert all((guest/name).read_bytes()==data for name,data in files.items())
        if not bad:
            assert (payload/core).read_bytes()!=b'stale reference'
            assert (payload/sky).read_bytes()!=b'stale reference'
            merged=(payload/ioaccel).read_bytes()
            offset,size=struct.unpack_from('>II',merged,36)
            assert merged[offset:offset+size]==other
            new_compiler=payload/compiler.replace('/3902/','/3903/')
            assert b'AppleParavirtGPUMetal.bundle' in new_compiler.read_bytes()
            assert (new_compiler.parent/'Resources/current.txt').read_bytes()==b'current guest resource'
            assert b'/Versions/3903/MTLCompiler' in (payload/service).read_bytes()
            receipt=plistlib.loads((payload/'.VirtualIntelMacPreparation.plist').read_bytes())
            assert receipt['PassthroughOpenGL'] is True
            assert receipt['OpenGLBootSelection'] is True
            driver=payload/'Library/Extensions/PassthroughOpenGLTransport.kext'
            assert driver.exists()
            if not bad:
                assert (payload/engine).read_bytes()==raw+b'_VIMOpenGLBootSelection\0'
                assert (payload/engine).with_name('GLEnginePassthrough').read_bytes()==optional_engine
                assert other in (payload/engine).with_name('GLEngineApple').read_bytes()
                saved=payload/'System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/PassthroughOpenGL/OriginalGLEngine'
                assert b'_VIMGLINotifyCPUSurfaceCopy\0' not in saved.read_bytes()
                assert other in saved.read_bytes() and raw in saved.read_bytes()
                assert saved.stat().st_mode&0o777==0o644
        print('PASS real staging:', 'unknown SkyLight refused' if bad else
              'optional Mojave engine/transport, ordinary renderer retained' if passthrough else
              'current guest images/resources prepared without reference seeds' if seedless else
              'current guest images/resources replace stale seeds')
