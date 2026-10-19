#!/usr/bin/env python3
"""Compile fixed guest target policy from the checked-in release lists."""
from pathlib import Path
import json
import sys

source = Path(__file__).resolve().parents[1]
output = Path(sys.argv[1])
lines = ['// Generated from support/install-targets*.txt; do not edit.']
for guest, filename in [('Catalina', 'install-targets.txt'), ('Mojave', 'install-targets-mojave.txt'), ('BigSur', 'install-targets-bigsur.txt')]:
    targets = (source/'support'/filename).read_text().splitlines()
    assert targets and len(targets) == len(set(targets))
    assert all((p.startswith(('/System/Library/', '/Library/Extensions/', '/Library/GPUBundles/')) or
                p in ('/usr/lib/PVGGLIOKit.dylib', '/Library/Preferences/com.apple.Aperture.plist',
                      '/Library/Preferences/com.apple.iPhoto.plist'))
               and '..' not in Path(p).parts for p in targets)
    lines.append(f'static NSArray<NSString *> *VIM{guest}InstallTargets(void) {{ return @[')
    lines.extend('@' + json.dumps(p[1:]) + ',' for p in targets)
    lines.append(']; }')
with output.open('x') as stream:
    stream.write('\n'.join(lines)+'\n')
