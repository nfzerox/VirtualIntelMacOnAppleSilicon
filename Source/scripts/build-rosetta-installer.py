#!/usr/bin/env python3
"""Package a tiny Intel app for macOS's normal Rosetta installation prompt.

This intentional Intel-only exception is an installation trigger, not a new
translated service. Compilation works natively on Intel or Apple silicon.
"""
from pathlib import Path
import plistlib
import shutil
import subprocess


def build(source, app, signing_identity):
    source, app = Path(source), Path(app)
    bundle = app/'Contents/Helpers/Rosetta Installer.app'
    executable = bundle/'Contents/MacOS/VIMRosettaInstaller'
    executable.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['xcrun', 'clang', '-target', 'x86_64-apple-macos11.0',
                    '-O2', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                    '-Werror=unguarded-availability-new',
                    str(source/'tools/RosettaInstaller/main.m'), '-framework', 'AppKit',
                    '-o', str(executable)], check=True)
    info = plistlib.loads((source/'tools/RosettaInstaller/Info.plist').read_bytes())
    parent = plistlib.loads((app/'Contents/Info.plist').read_bytes())
    icon_name = parent.get('CFBundleIconName', '')
    icon = app/'Contents/Resources'/(icon_name+'.icns')
    if icon_name and icon.is_file():
        resources = bundle/'Contents/Resources'
        resources.mkdir(exist_ok=True)
        shutil.copyfile(icon, resources/'AppIcon.icns')
        info['CFBundleIconFile'] = 'AppIcon.icns'
    with (bundle/'Contents/Info.plist').open('wb') as stream:
        plistlib.dump(info, stream)
    subprocess.run(['/usr/bin/codesign', '--force', '--sign', signing_identity,
                    '--options', 'runtime', '--timestamp=none', str(bundle)], check=True)
    subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(bundle)], check=True)
    return bundle
