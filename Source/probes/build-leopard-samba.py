#!/usr/bin/env python3
"""Research build of Apple's native Leopard or Snow Leopard Samba.

No installation, service changes, upstream-patch replay or host header fallback.
Uses Apple's checked-in generated configure, not a modern autoconf rewrite.
Build outside the source tree; only the supplied actual10.5 SDK is a guest ABI.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True,help='Apple source root for the selected target')
p.add_argument('--target',choices=('leopard','snowleopard'),default='leopard')
p.add_argument('--cf-source',type=Path,help='matching Apple CF sources for Snow Leopard private headers')
p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--cc',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--jobs',type=int,default=4)
p.add_argument('--configure-only',action='store_true')
p.add_argument('--build-only',action='store_true')
p.add_argument('--auth-module',action='store_true',help='Also build the matching native OD authentication module')
a=p.parse_args()
source=a.source.resolve();sdk=a.sdk.resolve();cc=a.cc.resolve();output=a.output.resolve()
if not 1<=a.jobs<=8:p.error('Choose1–8 build jobs')
for path in [source/'config.site.leopard',source/'samba/source/configure',
             source/'samba/source/auth/auth_odsam.c',sdk/'usr/include/ldap.h',
             sdk/'usr/include/DirectoryService/DirectoryService.h',cc]:
    # The actual SDK exposes DirectoryService through its framework search path.
    if path==sdk/'usr/include/DirectoryService/DirectoryService.h':
        path=sdk/'System/Library/Frameworks/DirectoryService.framework/Headers/DirectoryService.h'
    if not path.is_file():p.error('Required actual build input unavailable: '+str(path))
if output==source or source in output.parents:p.error('Use an output outside the source tree')
if not a.build_only:
    if output.exists():p.error('Use a new build directory, or --build-only after a successful configure')
    output.mkdir(parents=True)
    minimum,kernel={'leopard':('10.5','9'),'snowleopard':('10.6','10')}[a.target]
    target=['-arch','i386','-isysroot',str(sdk),'-mmacosx-version-min='+minimum]
    policy=['-DWITH_OPENDIRECTORY','-DWITH_MEMBERD','-DUSES_KEYCHAIN','-DWITH_SACL',
            '-DKRB5_PRIVATE=1'] # The actual SDK guards this project's native krb5 API declarations.
    env=dict(os.environ,CONFIG_SITE=str(source/'config.site.leopard'),CC=str(cc),
        # Match period GCC's C common-symbol semantics for this source;
        # tentative globals in the original headers are not separate strong definitions.
        CFLAGS=shlex.join(['-O2','-fcommon',*target,*policy]),
        CPP=shlex.join([str(cc),'-E',*target]),CPPFLAGS=shlex.join(policy),
        LDFLAGS=shlex.join([*target,'-framework','CoreFoundation','-framework','Security']))
    if a.target=='snowleopard':
        # The 10.6 SDK exports the old name for binary compatibility but
        # declares only enctypes. Select Samba's existing declared fallback.
        env['ac_cv_func_ext_krb5_set_default_tgs_ktypes']='no'
    modules='pdb_ldap,idmap_ldap,idmap_rid,idmap_ad,idmap_odsam,pdb_odsam,auth_unix,auth_winbind,auth_server,auth_domain,auth_ods,vfs_recycle,vfs_audit,vfs_extd_audit,vfs_full_audit,vfs_readonly,vfs_expand_msdfs,vfs_shadow_copy,vfs_darwin_streams,vfs_darwinacl,vfs_cacheprime,vfs_commit,vfs_catia,vfs_prealloc'
    # The generated 2007 config.sub predates arm64. The build compiler
    # is Intel-capable, and configure must treat i386 as cross-compiled.
    args=[str(source/'samba/source/configure'),
        '--build=x86_64-apple-darwin',
        '--host=i386-apple-darwin'+kernel,'--prefix=/usr','--with-configdir=/private/etc',
        '--with-privatedir=/private/var/db/samba','--with-libdir=/usr/lib/samba',
        '--with-lockdir=/private/var/samba','--with-logfilebase=/private/var/log/samba',
        '--with-piddir=/private/var/run','--disable-swat','--with-krb5','--with-ads',
        '--with-cups','--with-ldap','--with-libiconv=/usr','--with-sendfile-support',
        '--without-readline','--disable-pie','--with-winbind','--with-pam',
        '--with-libmsrpc','--with-libsmbclient','--with-libsmbsharemodes','--with-quotas',
        '--with-sys-quotas','--with-profiling-data','--with-kdebug-trace',
        '--enable-launchd','--with-dnsupdate','--with-shared-modules='+modules]
    subprocess.run(args,cwd=output,env=env,check=True)
if not (output/'Makefile').is_file():p.error('Successful configure required')
if not a.configure_only:
    # Upstream's standalone bin/smbd target is not a parallel-safe substitute
    # for its public-header prerequisite target in a fresh VPATH directory.
    relative=os.path.relpath(source/'samba/source',output)
    build_flags=['srcdir='+relative,'VPATH='+relative,'MKPROTO_SH=script/mkproto.sh']
    subprocess.run(['make',*build_flags,'proto'],cwd=output,check=True)
    makefile=(output/'Makefile').read_text()
    cflags=re.search(r'^CFLAGS=(.*)$',makefile,re.M)[1]
    cppflags=re.search(r'^CPPFLAGS=(.*)$',makefile,re.M)[1]
    cppflags=cppflags.replace('-F/System/Library/PrivateFrameworks',
        shlex.join(['-F'+str(sdk/'System/Library/PrivateFrameworks')]))
    if a.target=='snowleopard':
        if not a.cf_source:p.error('Snow Leopard requires --cf-source for period private headers')
        cf=output/'PeriodHeaders/CoreFoundation'
        cf.mkdir(parents=True,exist_ok=True)
        for header in ('CFStringEncodingConverter.h','CFUnicodePrecomposition.h','CFUniChar.h'):
            original=(a.cf_source/header).resolve()
            link=cf/header
            if not original.is_file():p.error('Missing period CF header: '+str(original))
            if not link.is_symlink():link.symlink_to(original)
            if link.resolve()!=original:p.error('Use a new output for different period CF headers')
        cppflags+=' '+shlex.join(['-I'+str(cf.parent)])
    # Configure intentionally compiles implicit-declaration availability probes
    # (including its misspelled launchd_msg test); enforce prototypes on REAL
    # source units instead, where a missing SDK declaration must fail closed.
    targets=['bin/smbd']+(['bin/auth_odsam.dylib'] if a.auth_module else [])
    subprocess.run(['make','-j'+str(a.jobs),*build_flags,'CFLAGS='+cflags+' -Werror=implicit-function-declaration',
        'CPPFLAGS='+cppflags,*targets],cwd=output,check=True)
    subprocess.run(['file',str(output/'bin/smbd')],check=True)
