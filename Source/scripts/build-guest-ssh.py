#!/usr/bin/env python3
"""Build only the modern SSH CLIENT for Intel OS X guests, never a daemon.

All upstream source is ordinary vendored files. Outputs and crypto staging stay
inside Build; nothing is installed into the builder or its SSH configuration.
The normal app build consumes the checked-in guest payload instead.
"""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess


def run(*args, cwd=None, env=None):
    subprocess.run([str(x) for x in args], cwd=cwd, env=env, check=True)


def main():
    source = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, default=source/'resources/sdk/MacOSX10.9.sdk')
    p.add_argument('--cc', default=os.environ.get('VIM_OPENGL_CC', str(source/'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang')))
    p.add_argument('--build', type=Path, default=Path(os.environ.get('VIM_GUEST_SSH_BUILD',str(source/'Build/GuestSSH'))))
    p.add_argument('--output', type=Path, default=source/'Build/OpenGLGuestAgent/guest-ssh')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--rebuild', action='store_true', help='Recompile instead of reusing the checked-in client')
    p.add_argument('--minimum',choices=('10.4','10.5','10.6','10.7'),default='10.7')
    p.add_argument('--ld', type=Path, help='Period Darwin linker (required for the native Tiger i386 client)')
    a = p.parse_args()
    sdk=a.sdk.resolve(); build=a.build.resolve(); output=a.output.resolve()
    if not (sdk/'usr/include/stdio.h').is_file() or not shutil.which(a.cc):
        p.error('Choose an Intel-capable compiler and a period Intel OS X SDK.')
    if not 1 <= a.jobs <= 32:
        p.error('--jobs must be between 1 and 32')
    architecture='i386' if a.minimum=='10.4' else 'x86_64'
    linker_flags=''
    if a.minimum=='10.4':
        if not a.ld or not a.ld.is_file():
            p.error('Tiger requires --ld pointing to a period Intel-capable Darwin linker.')
        if plistlib.loads((sdk/'SDKSettings.plist').read_bytes()).get('Version')!='10.4':
            p.error('Tiger requires the genuine MacOSX10.4u SDK.')
        # Let clang select genuine crt1/gcc_s.10.4, but do not send a period
        # linker modern ld64 options such as -no_deduplicate or -lto_library.
        # Older ld otherwise prefers SDK dylibs across every search path
        # before our static libcrypto. Tiger's system libcrypto is not the
        # vendored modern library used by this isolated client.
        linker_flags=f' -B{a.ld.resolve().parent} -mlinker-version=97 -Wl,-search_paths_first'
    prebuilt=source/'prebuilt/PassthroughOpenGL/mavericks/GuestTools'
    if a.minimum=='10.7' and not a.rebuild and (prebuilt/'guest-ssh').is_file():
        output.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(prebuilt/'guest-ssh',output);output.chmod(0o755)
        print(output);return
    if any(any(c.isspace() for c in str(path)) for path in (source,sdk,build,a.cc)):
        p.error('The upstream Autotools build requires source, compiler, SDK and build paths without whitespace.')
    build.mkdir(parents=True, exist_ok=True)
    env=os.environ.copy()
    # Keep __FILE__ strings relative too: source/configure paths below are
    # relative to the build tree, never the developer's home directory.
    target_flags=f'-arch {architecture} -isysroot {sdk} -mmacosx-version-min={a.minimum}{linker_flags}'
    env.update(CC=a.cc, CFLAGS=f'-O2 {target_flags}',
               LDFLAGS=f'{target_flags} -Wl,-dead_strip')
    if a.minimum=='10.4':
        compatibility=build/'tiger-ssh-compat.o'
        run(a.cc,'-arch','i386','-isysroot',sdk,'-mmacosx-version-min=10.4',
            '-O2','-c',source/'tools/guest-tiger-ssh-compat.c','-o',compatibility)
        env['LIBS']=str(compatibility)
    # This client also runs on Lion. A 10.9 SDK / newer builder can link the
    # later memset_s even with a 10.7 deployment target; Autoconf then selects
    # an unconditional call and Lion dies in dyld before negotiating SSH.
    # Use upstream's optimizer-resistant explicit_bzero fallback instead.
    env['ac_cv_func_memset_s']='no'
    crypto=build/'crypto'; ssh=build/'openssh'; stage=build/'stage'
    for directory in (crypto,ssh): directory.mkdir(exist_ok=True)
    if a.rebuild:
        # Make does not track changes in compiler/hardening flags. An explicit
        # rebuild must not silently reuse objects from a different target.
        for directory in (crypto,ssh):
            if (directory/'Makefile').exists():
                run('make','clean',cwd=directory,env=env)
    # Target the standard prefix in compiled-in constants. DESTDIR, not prefix,
    # isolates installation to our staging tree; no builder paths reach guests.
    run(os.path.relpath(source/'vendor/source/libressl/configure',crypto),
        f'--host={architecture}-apple-darwin','--prefix=/usr/local','--with-openssldir=/etc/ssl',
        '--disable-shared','--enable-static','--disable-tests',
        # LibreSSL's compiler-flag probe replaces CFLAGS, losing the target
        # SDK. It then selects host stack-protector symbols Tiger lacks.
        # This affects only the isolated 10.4 client, not host/sshd policy.
        *(['--disable-hardening'] if a.minimum=='10.4' else []),cwd=crypto,env=env)
    run('make',f'-j{a.jobs}','-C','include',cwd=crypto,env=env)
    run('make',f'-j{a.jobs}','-C','crypto',cwd=crypto,env=env)
    for part in ('include','crypto'):
        run('make','-C',part,'install',f'DESTDIR={stage}',cwd=crypto,env=env)
    run(os.path.relpath(source/'vendor/source/openssh/configure',ssh),
        f'--host={architecture}-apple-darwin',f'--with-ssl-dir={stage}/usr/local',
        '--prefix=/usr/local','--sysconfdir=/etc/ssh','--disable-security-key',
        '--without-pam','--without-libedit','--with-sandbox=no',
        '--with-privsep-path=/var/empty',cwd=ssh,env=env)
    run('make',f'-j{a.jobs}','ssh',cwd=ssh,env=env)
    output.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ssh/'ssh',output);output.chmod(0o755)
    run('strip','-S',output)
    run('codesign','--force','--sign','-','--digest-algorithm=sha1,sha256',output)
    run('codesign','--verify','--strict',output)
    deps=subprocess.check_output(['otool','-L',str(output)],text=True)
    if any(not line.strip().startswith(('/usr/lib/','/System/Library/'))
           for line in deps.splitlines()[1:] if line.startswith('\t')):
        raise RuntimeError('Guest SSH has an unpackaged library dependency')
    imports=subprocess.check_output(['nm','-u',str(output)],text=True).splitlines()
    if '_memset_s' in {line.strip() for line in imports}:
        raise RuntimeError('Guest SSH imports memset_s, which is unavailable on Lion')
    print(output)


if __name__=='__main__':
    main()
