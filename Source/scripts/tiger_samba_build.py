#!/usr/bin/env python3
"""Native Darwin8/i386 Apple Samba compilation; never install a service.

Uses the genuine10.4u SDK and an Intel-executable period compiler/linker.
Xcode4.6.3's clang/ld136 can link its prebound libraries; modern ld cannot.
The native Apple account/PAM/Kerberos/VFS policy is not substituted with a
separate password database. No host headers or modern autoconf are used.
"""
import argparse
import os
from pathlib import Path
import plistlib
import re
import shlex
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','cc','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--source',type=Path)
    p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--configure-only',action='store_true')
    p.add_argument('--build-only',action='store_true')
    p.add_argument('--reconfigure',action='store_true',help='Resume configuration in an existing research build directory')
    p.add_argument('--auth-module',action='store_true',help='Build the matching native DirectoryService authentication module')
    a=p.parse_args();root=Path(__file__).resolve().parents[1]
    vendor=(a.source or root/'vendor/source/samba-100.9').resolve()
    sdk,cc,out=(path.resolve() for path in (a.sdk,a.cc,a.output))
    if not 1<=a.jobs<=8:p.error('Choose1–8 build jobs')
    if plistlib.loads((sdk/'SDKSettings.plist').read_bytes()).get('Version')!='10.4':
        p.error('Supply the genuine MacOSX10.4u SDK')
    flags=['-arch','i386','-mmacosx-version-min=10.4','-isysroot',str(sdk)]
    # Original iconv configure unconditionally disables its native backend
    # when cross-compiling, even with a cache entry. The read-only actual
    # Tiger probe verifies CP850/437/UTF8/UTF8-MAC against this SDK's libiconv.
    policy=['-DWITH_OPENDIRECTORY','-DWITH_MEMBERD','-DUSES_KEYCHAIN','-DWITH_SACL',
            '-DKRB5_DEPRECATED=1','-DHAVE_NATIVE_ICONV=1']
    def run(command,**kwargs):
        print(shlex.join(map(str,command)),flush=True)
        subprocess.run(list(map(str,command)),check=True,**kwargs)
    if not a.build_only:
        out.mkdir(parents=True,exist_ok=a.reconfigure)
        if a.reconfigure:
            # The original make rules do not track config.h. A changed guest
            # capability selection must never reuse old compiled branches.
            for obj in out.rglob('*.o'):obj.unlink()
    run([cc,*flags,'-O2','-fcommon','-DUSES_KEYCHAIN=1','-fPIC','-c',
         os.path.relpath(vendor/'libopendirectorycommon/libopendirectorycommon.c',out),
         '-o',out/'libopendirectorycommon.o'],cwd=out)
    run(['xcrun','ar','rcs',out/'libopendirectorycommon.a',out/'libopendirectorycommon.o'])
    if not a.build_only:
        env=dict(os.environ,CONFIG_SITE=str(vendor/'config.site.tiger'),CC=str(cc),CFLAGS=shlex.join(['-O2','-fcommon',*flags,*policy]),
                 CPP=shlex.join([str(cc),'-E',*flags]),
                 CPPFLAGS=shlex.join([*policy,'-I'+str(vendor/'libopendirectorycommon'),'-I'+str(vendor/'compat')]),
                 LDFLAGS=shlex.join([*flags,'-L'+str(out),'-lopendirectorycommon',
                    '-framework','DirectoryService','-framework','Security','-framework','CoreFoundation','-liconv']))
        configure=[os.path.relpath(vendor/'samba/source/configure',out),
            '--build=x86_64-apple-darwin','--host=i386-apple-darwin8','--prefix=/usr',
            '--with-configdir=/private/etc','--with-privatedir=/private/var/db/samba',
            '--with-libdir=/usr/lib/samba','--with-lockdir=/private/var/samba',
            '--with-logfilebase=/private/var/log/samba','--with-piddir=/private/var/run',
            '--disable-swat','--with-krb5','--with-ads','--with-cups','--with-ldap',
            '--with-libiconv=/usr','--without-readline','--disable-shared',
            '--without-libsmbclient','--with-winbind','--with-pam']
        run(configure,cwd=out,env=env)
    if not a.configure_only:
        relative='srcdir='+os.path.relpath(vendor/'samba/source',out)
        run(['make',relative,'MKPROTO_SH=script/mkproto.sh','proto'],cwd=out)
        text=(out/'Makefile').read_text()
        cflags=re.search(r'^CFLAGS=(.*)$',text,re.M)[1]
        cppflags=re.search(r'^CPPFLAGS=(.*)$',text,re.M)[1].replace('-I/sw/include','')+' -I'+shlex.quote(str(vendor/'compat'))
        ldflags=re.search(r'^LDFLAGS=(.*)$',text,re.M)[1].replace('-L/sw/lib','')
        dependencies=out/'VIM-Dependencies.mk'
        dependencies.write_text('-include '+' '.join(p.relative_to(out).as_posix() for p in sorted(out.rglob('*.d')))+'\n')
        # The original Darwin configure adds Fink search paths outside the
        # supplied SDK. They are not this native build's dependencies.
        # Period ld can leave an older, larger output's trailing bytes behind
        # when relinking on a modern host. Link to a fresh derived file, not a
        # patched/truncated Mach-O after the fact.
        (out/'bin/smbd').unlink(missing_ok=True)
        run(['make','-f','Makefile','-f',dependencies.name,'-j'+str(a.jobs),relative,'MKPROTO_SH=script/mkproto.sh',
             'CFLAGS='+cflags+' -MMD -MP -Werror=implicit-function-declaration',
             'CPPFLAGS='+cppflags,'LDFLAGS='+ldflags,'bin/smbd'],cwd=out)
        run(['file',out/'bin/smbd'])
        if a.auth_module:
            # Match Apple's native plugin ABI, without its development-only
            # DEBUG_PASSWORD/DEVELOPER switches or a new credential database.
            native=vendor/'samba/source'
            includes=[out/'include',native/'include',native/'ubiqx',native/'smbwrapper',
                      native/'popt',native,out,vendor/'libopendirectorycommon',vendor/'compat']
            run([cc,*shlex.split(cflags),*shlex.split(cppflags),
                 *['-I'+str(path) for path in includes],
                 '-Werror=implicit-function-declaration','-c',os.path.relpath(vendor/'auth_ods/auth_ods.c',out),
                 '-o',out/'auth_ods.o'],cwd=out)
            (out/'bin/opendirectory.so').unlink(missing_ok=True)
            run([cc,*flags,'-bundle','-twolevel_namespace',
                 '-Wl,-exported_symbols_list,'+str(vendor/'auth_ods/auth_ods.exp'),
                 '-bundle_loader',out/'bin/smbd',out/'auth_ods.o',out/'libopendirectorycommon.a',
                 '-framework','DirectoryService','-framework','Security','-framework','CoreFoundation',
                 '-o',out/'bin/opendirectory.so'])

if __name__=='__main__':main()
