#!/usr/bin/env python3
"""Qualify known Big Sur imports without weakening the newer-API guard."""
from pathlib import Path
from unittest.mock import patch
import subprocess
import deployment_target as target

locks = ['_os_unfair_lock_lock', '_os_unfair_lock_trylock', '_os_unfair_lock_unlock']
with patch.object(target.subprocess, 'check_output', return_value='arm64 x86_64'), \
     patch.object(target, '_weak_libsystem_imports', return_value=locks):
    target.check_weak_libsystem_imports(Path('fixture'))

for name in ('_pipe2', '_dup3', '_os_unfair_lock_future_api'):
    with patch.object(target.subprocess, 'check_output', return_value='arm64 x86_64'), \
         patch.object(target, '_weak_libsystem_imports', return_value=[name]):
        try:
            target.check_weak_libsystem_imports(Path('fixture'))
        except RuntimeError as error:
            assert name in str(error)
        else:
            raise AssertionError('Accepted an unqualified weak import: ' + name)

source = '''#include <os/lock.h>
void fixture(os_unfair_lock_t lock) {
    os_unfair_lock_lock(lock);
    os_unfair_lock_unlock(lock);
    if (os_unfair_lock_trylock(lock)) os_unfair_lock_unlock(lock);
}
'''
for arch in ('arm64', 'x86_64'):
    subprocess.run(target._flags(arch, '11.0'), input=source, text=True, check=True)
print('PASS known unfair-lock imports, unknown/newer rejection, native SDK macOS 11 availability on both architectures')
