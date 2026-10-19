#!/usr/bin/env python3
"""Small fixtures: invalidation and integrity, no QEMU build or guest boot."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('freshness', Path(__file__).with_name('build_freshness.py'))
freshness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(freshness)

with tempfile.TemporaryDirectory(prefix='vim-freshness-test-') as temporary:
    root = Path(temporary)
    source = root/'Source'
    qemu = source/'vendor/qemu'
    qemu.mkdir(parents=True)
    subprocess.run(['git', 'init', '-q', root], check=True)
    (root/'.gitignore').write_text('Build/\n')
    (qemu/'machine.c').write_text('original')
    helper = source/'compat/PVGHostAIRRepair.m'
    helper.parent.mkdir(); helper.write_text('helper')
    recipe = source/'scripts/build-app-runtime.py'
    recipe.parent.mkdir(); recipe.write_text('recipe')
    archive = source/'vendor/qemu-dependency-sources.tar.gz'
    archive.write_bytes(b'archive')
    with patch.object(freshness, 'toolchain', return_value={'Compiler': 'fixture'}):
        before = freshness.snapshot(source, 'runtime')
        binary = root/'Build/qemu'; binary.parent.mkdir(); binary.write_bytes(b'code')
        receipt = {'Freshness': freshness.seal(before, before, binary.parent, [binary])}
        assert freshness.current(receipt, before, binary.parent, ('qemu',))
        assert not freshness.current({}, before, binary.parent)
        assert not freshness.current([], before, binary.parent)
        assert not freshness.current({'Freshness':[]}, before, binary.parent)
        assert not freshness.current(receipt, before, binary.parent, ('missing',))
        binary.write_bytes(b'corrupt')
        assert not freshness.current(receipt, before, binary.parent)
        binary.write_bytes(b'code')
        binary.chmod(0o755)
        assert not freshness.current(receipt, before, binary.parent)
        binary.chmod(0o644)
        binary.unlink()
        assert not freshness.current(receipt, before, binary.parent)
        for path in (qemu/'machine.c', helper, recipe, archive):
            old = path.read_bytes(); path.write_bytes(old+b'changed')
            assert freshness.snapshot(source, 'runtime') != before, path
            path.write_bytes(old)
        added = qemu/'new.h'; added.write_text('new')
        assert freshness.snapshot(source, 'runtime') != before
        added.unlink()
        ignored = qemu/'Build'; ignored.mkdir(); (ignored/'temporary.o').write_text('ignored')
        assert freshness.snapshot(source, 'runtime') == before
        (qemu/'machine.c').unlink()
        assert freshness.snapshot(source, 'runtime') != before
        (qemu/'machine.c').write_text('original')
        relocated = root/'Relocated'; shutil.copytree(source, relocated)
        assert freshness.snapshot(relocated, 'runtime') == before
        with patch.object(freshness, 'toolchain', return_value={'Compiler': 'new'}):
            assert freshness.snapshot(source, 'runtime') != before
        try:
            freshness.seal(before, {}, root, [])
        except RuntimeError:
            pass
        else:
            raise AssertionError('Accepted inputs changing during compilation')
print('PASS: source/helper/recipe/dependency/toolchain changes, new/deleted files, corrupt/missing outputs, legacy receipts, relocation and build races')
