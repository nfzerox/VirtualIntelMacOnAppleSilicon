#!/usr/bin/env python3
"""Stage qualified native cold controls into a STOPPED generated Tiger DVD.

Development handoff only, not a substitute for the full production support
payload. No original DMG or installed OS is edited. The native rc.cdrom.local
hook starts the service and prepares an explicitly enabled native minstall
policy before the language picker. No account or graphics driver is replaced.
"""
import argparse
import hashlib
from pathlib import Path
import os
import plistlib
import subprocess
import tempfile


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--job',type=Path,required=True)
    p.add_argument('--tools',type=Path,required=True)
    a=p.parse_args();source=Path(__file__).resolve().parents[1]
    record=a.job/'Media.plist';media=plistlib.loads(record.read_bytes())
    if (media.get('Guest')!='tiger' or media.get('ReadbackVerified') is not True or
        media.get('TigerRestorePolicyVersion')!=1 or media.get('Media')!='Installer.iso' or media.get('TigerControlBootstrapVersion') not in (None,1)):
        raise ValueError('Use an intact prepared Tiger media job, optionally with its version1 control hook')
    image=a.job/media['Media']
    if image.is_symlink() or not image.is_file() or image.stat().st_size!=media['Size']:
        raise ValueError('Prepared installer image does not match its receipt')
    opened=subprocess.run(['/usr/sbin/lsof','-t',str(image)],capture_output=True,text=True)
    if opened.returncode!=1 or opened.stdout.strip():raise ValueError('Stop all users of this generated installer before staging controls')
    receipt=plistlib.loads((a.tools/'BuildReceipt.plist').read_bytes())
    if receipt.get('MinimumOS')!='10.4' or receipt.get('Architecture')!='i386':raise ValueError('Actual Tiger i386 tools are required')
    names=('opengl-guest-agent','opengl-tiger-installer-bootstrap','opengl-tiger-install-config')
    payload={}
    for name in names:
        data=(a.tools/name).read_bytes()
        payload[name]=data
    with tempfile.TemporaryDirectory(prefix='vim-tiger-control-') as temp:
        mount=Path(temp)/'Mount';mount.mkdir()
        def attach(readonly):
            subprocess.run(['/usr/bin/hdiutil','attach','-readonly' if readonly else '-readwrite','-owners','off',
                '-nobrowse','-noautoopen','-mountpoint',str(mount),str(image)],check=True)
        def detach():subprocess.run(['/usr/bin/hdiutil','detach',str(mount)],check=True)
        attach(False)
        try:
            version=plistlib.loads((mount/'System/Library/CoreServices/SystemVersion.plist').read_bytes())
            if version.get('ProductBuildVersion')!='8R4088':raise ValueError('Wrong generated DVD build')
            dist=mount/'System/Installation/Packages/OSInstall.mpkg/Contents/OSInstall.dist'
            if hashlib.sha256(dist.read_bytes()).hexdigest()!=media['PreparedDistributionSHA256']:
                raise ValueError('Prepared DVD distribution changed')
            rc=(mount/'etc/rc.cdrom').read_text()
            if rc.count('\t/etc/rc.cdrom.local\n')!=1:raise ValueError('Unrecognized native cold-start hook')
            hook=mount/'etc/rc.cdrom.local'
            config=mount/'etc/minstallconfig.xml'
            if config.exists() or config.is_symlink():raise ValueError('An existing native installer configuration was not replaced')
            postflight=mount/'System/Installation/Packages/OSInstall.mpkg/Contents/Resources/postflight'
            original=postflight.read_bytes()
            if hashlib.sha256(original).hexdigest()!='47cf5b22dc946b896e6f6f5290ece2173106bc61bd4fa9ccbb1831b260d8a96b' or not original.endswith(b'\nexit 0\n'):
                raise ValueError('Unrecognized native package postflight')
            # OSInstall replaces preexisting Library contents. Stage the
            # persistent service AFTER its payload, not only before it. The
            # compiled finalizer verifies minstall authorization/target UUID;
            # manual installs retain Apple's original postflight behavior.
            completion=(b'\nif [ -f /var/tmp/VirtualIntelMac-minstallconfig.plist ]; then\n'
                b'    /System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-install-config --finish-install || exit $?\n'
                b'fi\nexit 0\n')
            preparedPostflight=original[:-len(b'\nexit 0\n')]+completion
            extras=mount/'System/Installation/Packages/Extras/VirtualIntelMac'
            if media.get('TigerControlBootstrapVersion')==1:
                if (not hook.is_symlink() or os.readlink(hook)!='/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-installer-bootstrap' or
                    extras.is_symlink() or not extras.is_dir()):raise ValueError('The prior control hook is not intact')
                old=media.get('TigerControlFiles',{})
                if set(old)!={'opengl-guest-agent','opengl-tiger-installer-bootstrap'} or set(f.name for f in extras.iterdir())!=set(old):
                    raise ValueError('Unexpected files in prior control payload')
                for name,expected in old.items():
                    if (extras/name).is_symlink() or hashlib.sha256((extras/name).read_bytes()).hexdigest()!=expected:
                        raise ValueError('The prior control payload was changed')
            else:
                if hook.exists() or hook.is_symlink():raise ValueError('An existing installer hook was not replaced')
                extras.mkdir(parents=True,exist_ok=False)
                hook.symlink_to('/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-installer-bootstrap')
            for name,data in payload.items():
                (extras/name).write_bytes(data);os.chmod(extras/name,0o755)
            config.symlink_to('/var/tmp/VirtualIntelMac-minstallconfig.plist')
            postflight.write_bytes(preparedPostflight)
        finally:detach()
        attach(True)
        try:
            for name,data in payload.items():
                if (mount/'System/Installation/Packages/Extras/VirtualIntelMac'/name).read_bytes()!=data:
                    raise ValueError('Tiger control readback did not match')
            if os.readlink(mount/'etc/rc.cdrom.local')!='/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-installer-bootstrap':
                raise ValueError('Native hook did not read back correctly')
            if os.readlink(mount/'etc/minstallconfig.xml')!='/var/tmp/VirtualIntelMac-minstallconfig.plist':
                raise ValueError('Native minstall configuration link did not read back correctly')
            if postflight.read_bytes()!=preparedPostflight:raise ValueError('Native postflight handoff did not read back correctly')
        finally:detach()
    media.update(TigerControlBootstrapVersion=2,BootValidated=False,NativeAutomaticInstallerLanguage='English',
        NativePostflightSHA256=hashlib.sha256(preparedPostflight).hexdigest(),
        TigerControlFiles={name:hashlib.sha256(data).hexdigest() for name,data in payload.items()})
    temporary=record.with_suffix('.tmp');temporary.write_bytes(plistlib.dumps(media));temporary.replace(record)
    print('Tiger native control hook staged and read back; boot/handshake qualification remains required')


if __name__=='__main__':main()
