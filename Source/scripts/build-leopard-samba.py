#!/usr/bin/env python3
"""Build/reuse the native Leopard SMB compatibility component.

Ordinary builds copy the checked-in i386 output with its native ABI checks.
--rebuild uses the actual10.5 SDK and Intel-capable period Apple compiler.
Nothing is installed into the builder or a guest. No downloaded dependencies,
autoconf regeneration, guest framework extraction or host headers are used.
"""
import argparse
import os
from pathlib import Path
import plistlib
import re
import shlex
import shutil
import subprocess


def verify(directory, source):
    manifest = plistlib.loads((directory / 'LeopardSMB.plist').read_bytes())
    expected = {'leopard-smbd', 'leopard-odsam.dylib', 'Samba-COPYING.txt'}
    if (manifest.get('FormatVersion') != 1 or manifest.get('Guest') != 'leopard' or
            set(manifest.get('Files', {})) != expected):
        raise ValueError('Leopard SMB metadata is missing or invalid')
    for name in expected:
        if not (directory/name).is_file():
            raise ValueError('Leopard SMB payload is missing: ' + name)
    for name in expected - {'Samba-COPYING.txt'}:
        path = directory / name
        if subprocess.check_output(['lipo', '-archs', str(path)], text=True).split() != ['i386']:
            raise ValueError('Leopard SMB requires its native i386 ABI: ' + name)
        subprocess.run(['codesign', '--verify', '--strict', str(path)], check=True)
        if b'/Users/' in path.read_bytes() or b'/Volumes/' in path.read_bytes():
            raise ValueError('Builder paths leaked into Leopard SMB: ' + name)
    return manifest


def compile_native(vendor, build, sdk, jobs):
    # Configure makes srcdir absolute; most VPATH compile rules stay relative,
    # but its explicit pidfile rule otherwise leaks the builder path in __FILE__.
    relative = 'srcdir=' + os.path.relpath(vendor / 'samba/source', build)
    # mkproto's recipe first cd's into srcdir, so its script path is then
    # source-relative (not build-relative). Output builddir remains explicit.
    subprocess.run(['make', relative, 'MKPROTO_SH=script/mkproto.sh', 'proto'], cwd=build, check=True)
    makefile = (build / 'Makefile').read_text()
    cflags = re.search(r'^CFLAGS=(.*)$', makefile, re.M)[1]
    cppflags = re.search(r'^CPPFLAGS=(.*)$', makefile, re.M)[1]
    cppflags = cppflags.replace('-F/System/Library/PrivateFrameworks', shlex.join(['-F' + str(sdk / 'System/Library/PrivateFrameworks')]))
    subprocess.run(['make', '-j' + str(jobs), relative, 'MKPROTO_SH=script/mkproto.sh', 'CFLAGS=' + cflags + ' -Werror=implicit-function-declaration',
                    'CPPFLAGS=' + cppflags, 'bin/smbd', 'bin/auth_odsam.dylib'], cwd=build, check=True)


def publish(build, output, source):
    vendor = source / 'vendor/source/samba-187.8'
    output.mkdir(parents=True, exist_ok=True)
    for name, origin in [('leopard-smbd', 'bin/smbd'), ('leopard-odsam.dylib', 'bin/auth_odsam.dylib')]:
        target = output / name
        shutil.copyfile(build / origin, target); target.chmod(0o755)
        subprocess.run(['strip', '-S', str(target)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', '--digest-algorithm=sha1,sha256', str(target)], check=True)
    shutil.copyfile(vendor / 'samba/COPYING', output / 'Samba-COPYING.txt')
    names = ('leopard-smbd', 'leopard-odsam.dylib', 'Samba-COPYING.txt')
    manifest = {'FormatVersion': 1, 'Guest': 'leopard', 'PolicyVersion': 1,
                'UpstreamCommit': '8157479ed3e94c6403b024a9703162139e2e79a5',
                'Files': {name: True for name in names}}
    (output / 'LeopardSMB.plist').write_bytes(plistlib.dumps(manifest))
    verify(output, source)


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--cc', type=Path)
    parser.add_argument('--build', type=Path, default=source / 'Build/LeopardSambaBuild')
    parser.add_argument('--output', type=Path, default=source / 'Build/LeopardSamba')
    parser.add_argument('--verify', type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--rebuild', action='store_true')
    args = parser.parse_args()
    if args.verify:
        verify(args.verify, source); print('Leopard SMB source/bytes/signatures verified'); return
    if not 1 <= args.jobs <= 8:
        parser.error('Choose between1 and8 build jobs')
    prebuilt = source / 'prebuilt/LeopardSMB'
    if not args.rebuild:
        verify(prebuilt, source)
        if args.output.resolve() != prebuilt.resolve():
            args.output.mkdir(parents=True, exist_ok=True)
            for path in prebuilt.iterdir():
                if path.is_file():
                    shutil.copyfile(path, args.output / path.name)
                    (args.output / path.name).chmod(0o644 if path.suffix in ('.txt', '.plist') else 0o755)
        print(args.output); return
    if not args.sdk or not args.cc:
        parser.error('--rebuild requires --sdk and --cc')
    sdk = args.sdk.resolve(); cc = args.cc.resolve(); build = args.build.resolve()
    vendor = source / 'vendor/source/samba-187.8'
    for path in (sdk / 'usr/include/ldap.h', sdk / 'System/Library/Frameworks/DirectoryService.framework/Headers/DirectoryService.h', cc):
        if not path.is_file(): parser.error('Missing actual build input: ' + str(path))
    if build.exists(): parser.error('Use a new --build directory for a clean native ABI configuration')
    if args.output.resolve() == prebuilt.resolve(): parser.error('Build to a new output, then explicitly publish verified prebuilts')
    build.mkdir(parents=True)
    flags = ['-arch', 'i386', '-isysroot', str(sdk), '-mmacosx-version-min=10.5']
    policy = ['-DWITH_OPENDIRECTORY', '-DWITH_MEMBERD', '-DUSES_KEYCHAIN', '-DWITH_SACL', '-DKRB5_PRIVATE=1']
    env = dict(os.environ, CONFIG_SITE=str(vendor / 'config.site.leopard'), CC=str(cc),
               CFLAGS=shlex.join(['-O2', '-fcommon', *flags, *policy]),
               CPP=shlex.join([str(cc), '-E', *flags]), CPPFLAGS=shlex.join(policy),
               LDFLAGS=shlex.join([*flags, '-framework', 'CoreFoundation', '-framework', 'Security']))
    modules = 'pdb_ldap,idmap_ldap,idmap_rid,idmap_ad,idmap_odsam,pdb_odsam,auth_unix,auth_winbind,auth_server,auth_domain,auth_ods,vfs_recycle,vfs_audit,vfs_extd_audit,vfs_full_audit,vfs_readonly,vfs_expand_msdfs,vfs_shadow_copy,vfs_darwin_streams,vfs_darwinacl,vfs_cacheprime,vfs_commit,vfs_catia,vfs_prealloc'
    # The period Intel compiler runs natively or under Rosetta on arm64 builders.
    # All real C targets are cross-compiled i386; no Darwin9 executable is run.
    configure = [os.path.relpath(vendor / 'samba/source/configure', build),
        '--build=x86_64-apple-darwin', '--host=i386-apple-darwin9', '--prefix=/usr',
        '--with-configdir=/private/etc', '--with-privatedir=/private/var/db/samba',
        '--with-libdir=/usr/lib/samba', '--with-lockdir=/private/var/samba',
        '--with-logfilebase=/private/var/log/samba', '--with-piddir=/private/var/run',
        '--disable-swat', '--with-krb5', '--with-ads', '--with-cups', '--with-ldap',
        '--with-libiconv=/usr', '--with-sendfile-support', '--without-readline',
        '--disable-pie', '--with-winbind', '--with-pam', '--with-libmsrpc',
        '--with-libsmbclient', '--with-libsmbsharemodes', '--with-quotas', '--with-sys-quotas',
        '--with-profiling-data', '--with-kdebug-trace', '--enable-launchd',
        '--with-dnsupdate', '--with-shared-modules=' + modules]
    subprocess.run(configure, cwd=build, env=env, check=True)
    compile_native(vendor, build, sdk, args.jobs)
    publish(build, args.output, source); print(args.output)


if __name__ == '__main__':
    main()
