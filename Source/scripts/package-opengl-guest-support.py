#!/usr/bin/env python3
"""Publish compiled Mavericks support, without running/installing guest code.

Inputs are explicit build outputs; ordinary app builds consume the checked-in
directory without sealing it to a particular source checkout. No guest Xcode, network, Mesa,
research VM, stock guest framework extraction or borrowed i386 slice required.
"""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess


def verify(directory,source,check_sources=True):
    """Check the payload contract, not whether users have rebuilt/replaced it."""
    required=('GLEngine','libVIMPassthroughOpenGL.dylib','opengl-guest-agent',
              'opengl-install-bootstrap','guest-display','guest-clipboard',
              'VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA')
    if any(not (directory/name).is_file() for name in required):
        raise ValueError('OpenGL guest support is incomplete')
    if (directory/'LeopardSMB.plist').exists():
        import runpy
        runpy.run_path(str(source/'scripts/build-tiger-smb-client.py'))['verify'](directory,source,'leopard')
    # Return a transient inventory for packaging; no generated manifest in Git.
    return {'FormatVersion':1,'RequiresGuestDeveloperTools':False,'Files':{
        p.relative_to(directory).as_posix():True for p in sorted(directory.rglob('*'))
        if p.is_file() and p.name not in ('Manifest.plist','.DS_Store')}}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tools',type=Path)
    p.add_argument('--gl-pair',type=Path)
    p.add_argument('--ga',type=Path)
    p.add_argument('--output',type=Path)
    p.add_argument('--verify',type=Path)
    p.add_argument('--guest',choices=('highsierra','sierra','elcapitan','yosemite','mavericks','mountainlion','lion','snowleopard','leopard'),default='mavericks')
    a=p.parse_args();source=Path(__file__).resolve().parents[1]
    if a.verify:
        manifest=verify(a.verify,source)
        print('OpenGL guest support files available');return
    if not all((a.tools,a.gl_pair,a.ga,a.output)) or a.output.exists():p.error('Specify explicit inputs and a new output directory')
    binaries={name:a.tools/name for name in ('opengl-guest-agent','opengl-audio-defaults','opengl-create-account','opengl-install-bootstrap','opengl-setup-status','opengl-graphics-check',
        'opengl-installer-launch','opengl-installer-automation.dylib','opengl-display-agent','opengl-desktop-status','guest-display','guest-clipboard','guest-host-connection','guest-ssh')}
    binaries.update({'GLEngine':a.gl_pair/'GLEngine','libVIMPassthroughOpenGL.dylib':a.gl_pair/'libVIMPassthroughOpenGL.dylib',
                     'VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA':a.ga})
    if a.guest=='leopard':
        import runpy
        runpy.run_path(str(source/'scripts/build-leopard-samba.py'))['verify'](a.tools/'LeopardSMB',source)
        binaries.update({'opengl-file-sharing':a.tools/'opengl-file-sharing',
                         'leopard-smbd':a.tools/'LeopardSMB/leopard-smbd',
                         'leopard-odsam.dylib':a.tools/'LeopardSMB/leopard-odsam.dylib'})
    if a.guest=='snowleopard':
        snow=source/'prebuilt/PassthroughOpenGL/snowleopard/GuestTools'
        binaries.update({'opengl-file-sharing':a.tools/'opengl-file-sharing',
                         'snowleopard-smbd':snow/'snowleopard-smbd',
                         'snowleopard-odsam.dylib':snow/'snowleopard-odsam.dylib'})
    for name,binary in binaries.items():
        dual=name.startswith(('GLEngine','libVIM','VMsvga')) or (a.guest=='leopard' and name=='opengl-installer-automation.dylib')
        required={'i386'} if name in ('leopard-smbd','leopard-odsam.dylib','snowleopard-smbd','snowleopard-odsam.dylib') else {'x86_64','i386'} if dual else {'x86_64'}
        actual=set(subprocess.check_output(['lipo','-archs',str(binary)],text=True).split())
        if actual!=required:raise ValueError('Unexpected guest architectures: '+name)
        # The reusable GA input contains the signed Mach-O and source-generated
        # Info.plist, not the original build bundle's resource envelope. Still
        # verify its executable signature; don't require an unshipped envelope.
        signature=['codesign','--verify','--strict']
        if name.startswith('VMsvga2GA.plugin/'):
            signature += ['--ignore-resources']
        subprocess.run(signature+[str(binary)],check=True)
    a.output.mkdir(parents=True)
    for name,binary in binaries.items():
        target=a.output/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(binary,target);target.chmod(0o755)
    licenses=a.output/'Licenses';licenses.mkdir()
    for name,origin in (('OpenSSH-LICENCE.txt','openssh/LICENCE'),('LibreSSL-COPYING.txt','libressl/COPYING')):
        shutil.copyfile(source/'vendor/source'/origin,licenses/name)
    if a.guest in ('leopard','snowleopard'):
        shutil.copyfile(source/'vendor/source/JSONKit/JSONKit.h',licenses/'JSONKit-LICENSE.txt')
    if a.guest=='snowleopard':
        for name in ('SnowLeopardSMB.plist','Samba-COPYING.txt'):
            shutil.copyfile(snow/name,a.output/name)
    if a.guest=='leopard':
        for name in ('LeopardSMB.plist','Samba-COPYING.txt'):
            shutil.copyfile(a.tools/'LeopardSMB'/name,a.output/name)
        client=runpy.run_path(str(source/'scripts/build-tiger-smb-client.py'))
        client['verify'](a.tools/'LeopardSMBClient',source,'leopard')
        for name in (*client['client_files']('leopard'),'LeopardSMBClient.plist'):
            shutil.copyfile(a.tools/'LeopardSMBClient'/name,a.output/name)
    plugin=plistlib.loads((source/'vendor/source/VMsvga2/Info-GA.plist').read_bytes())
    values={'${EXECUTABLE_NAME}':'VMsvga2GA','${PRODUCT_NAME}':'VMsvga2GA',
            '${MODULE_NAME}':'net.osx86.VMsvga2GA','${MODULE_VERSION}':'1.2.5',
            '${COPYRIGHT_NOTICE}':'Copyright 2009-2013 Zenith432; Virtual Intel Mac contributors'}
    for key,value in plugin.items():
        if isinstance(value,str) and value in values:plugin[key]=values[value]
    (a.output/'VMsvga2GA.plugin/Contents/Info.plist').write_bytes(plistlib.dumps(plugin))
    shutil.copyfile(source/'PassthroughOpenGL/com.mac.virtualintelmac.opengl-agent.plist',a.output/'com.mac.virtualintelmac.opengl-agent.plist')
    shutil.copyfile(source/'PassthroughOpenGL/com.virtualintelmac.opengl-display.plist',a.output/'com.virtualintelmac.opengl-display.plist')
    verify(a.output,source);print(a.output)

if __name__=='__main__':main()
