#!/usr/bin/env python3
"""Build owned OS X graphics kexts using an explicit period-Xcode builder.

Only a fresh /var/tmp source/build folder is written on that machine. Never
installs/unloads a kext, modifies its system, or changes the running reference.
Requires a previously authenticated SSH master and matching period SDK.
The generated source archive and complete log accompany the resulting bundle.
"""
import argparse
from pathlib import Path
import shlex
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--builder', required=True, help='Explicit SSH user@host (not a guest alias)')
    p.add_argument('--ssh-control', required=True, type=Path)
    p.add_argument('--developer-dir', default='/Applications/Xcode5.app/Contents/Developer')
    p.add_argument('--sdk',choices=('macosx10.8','macosx10.9'),default='macosx10.9')
    p.add_argument('--target',action='append',choices=('VMsvga2','VMsvga2Accel'),
                   help='Repeat to build both framebuffer and accelerator; default accelerator only')
    p.add_argument('--output', required=True, type=Path, help='New local result directory')
    p.add_argument('--profile-backing-handoff', action='store_true',
                   help='Explicit finite source-clock laboratory build; never enabled by default')
    a=p.parse_args()
    if a.output.exists() or a.builder.startswith('-') or not a.ssh_control.is_socket():
        p.error('Use a new output directory, explicit builder and live authenticated SSH control socket')
    source=Path(__file__).resolve().parents[1]
    ssh=['ssh','-S',str(a.ssh_control),'-o','BatchMode=yes',a.builder]
    # Capture only selected current source; do not copy caches, .git or SDKs.
    a.output.mkdir(parents=True)
    archive=a.output/'source.tar.gz'
    # New macOS pax/xattr records are not understood by Mavericks' tar.
    subprocess.run(['env','COPYFILE_DISABLE=1','tar','--format=ustar','--no-xattrs','--no-acls','--no-fflags',
        '--exclude=.git','--exclude=build','--exclude=._*','--exclude=.DS_Store','-czf',str(archive),'-C',str(source),
        'vendor/source/VMsvga2','probes/opengl-map-object-profile.h',
        'probes/opengl-map-block-profile.h'],check=True)
    scratch=subprocess.check_output(ssh+['mktemp -d /var/tmp/vim-opengl-kernel.XXXXXX'],text=True).strip()
    if not scratch.startswith('/var/tmp/vim-opengl-kernel.') or '/' in scratch[len('/var/tmp/'):]:
        raise RuntimeError('Unexpected builder scratch path; stopped without installing anything')
    (a.output/'builder-scratch.txt').write_text(scratch+'\n')
    scp=['scp','-o','ControlPath='+str(a.ssh_control),'-o','BatchMode=yes']
    subprocess.run(scp+[str(archive),a.builder+':'+scratch+'/source.tar.gz'],check=True)
    targets=a.target or ['VMsvga2Accel']
    commands=[['env','LC_ALL=C','tar','-xzf',scratch+'/source.tar.gz','-C',scratch]]
    build=['env','LC_ALL=C','DEVELOPER_DIR='+a.developer_dir,'/usr/bin/xcodebuild',
         '-project',scratch+'/vendor/source/VMsvga2/VMsvga2.xcodeproj',
         '-configuration','ReleaseSnowLeo','-sdk',a.sdk,
         'ARCHS=x86_64','VALID_ARCHS=x86_64','MACOSX_DEPLOYMENT_TARGET='+a.sdk.removeprefix('macosx'),
         'CODE_SIGNING_ALLOWED=NO','COPY_PHASE_STRIP=NO','DEPLOYMENT_POSTPROCESSING=NO',
         'OBJROOT='+scratch+'/Objects','SYMROOT='+scratch+'/Products']
    for target in targets:build+=['-target',target]
    commands.append(build)
    if a.profile_backing_handoff:
        commands[-1].append('OTHER_CPLUSPLUSFLAGS=$(inherited) -DVIM_OPENGL_HANDOFF_PROFILE=1')
    with (a.output/'build-complete.log').open('w') as log:
        subprocess.run(ssh+[' && '.join(shlex.join(c) for c in commands)],stdout=log,stderr=subprocess.STDOUT,check=True)
    for target in targets:
        subprocess.run(scp+['-r',a.builder+':'+scratch+'/Products/ReleaseSnowLeo/'+target+'.kext',str(a.output)],check=True)
        binary=a.output/(target+'.kext')/'Contents/MacOS'/target
        subprocess.run(['dwarfdump','--uuid',str(binary)],check=True)
    print(a.output)


if __name__=='__main__':main()
