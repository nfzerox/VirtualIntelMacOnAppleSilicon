#!/usr/bin/env python3
"""Small packaging fixture; no firmware build or VM boot."""
from pathlib import Path
import tempfile
from firmware_layout import share_identical_code

with tempfile.TemporaryDirectory(prefix='vim-firmware-layout-') as temporary:
    root = Path(temporary).resolve()
    for guest, data in (('bigsur', b'common-code'), ('catalina', b'common-code'),
                        ('mojave', b'common-code'), ('variant', b'different-code')):
        folder = root/guest
        folder.mkdir()
        (folder/'CODE.fd').write_bytes(data)
        (folder/'VARS.fd').write_bytes(b'writable-seed')
    assert share_identical_code(root) == 2*len(b'common-code')
    for guest in ('bigsur', 'catalina', 'mojave'):
        code = root/guest/'CODE.fd'
        assert not code.exists() and not code.is_symlink()
        assert (root/'Common/CODE.fd').read_bytes() == b'common-code'
        assert not (root/guest/'VARS.fd').is_symlink()
    assert (root/'variant/CODE.fd').read_bytes() == b'different-code'
    assert not (root/'variant/CODE.fd').is_symlink()
    assert share_identical_code(root) == 0
    relocated = root.with_name(root.name+'-relocated')
    root.rename(relocated)
    try:
        assert (relocated/'Common/CODE.fd').read_bytes() == b'common-code'
        assert not (relocated/'bigsur/CODE.fd').exists()
    finally:
        relocated.rename(root)
print('PASS: app-local common read-only code, variants retained, writable NVRAM untouched')
