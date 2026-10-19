#!/usr/bin/env python3
"""Check the fast-build cache gate without building QEMU, Xcode or payloads."""
import ast
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).with_name('build-app')
tree = ast.parse(source.read_text())
function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                and node.name == 'runtime_matches_checkout')
namespace = {'subprocess': subprocess}
exec(compile(ast.Module(body=[function], type_ignores=[]), str(source), 'exec'), namespace)
check = namespace['runtime_matches_checkout']

with tempfile.TemporaryDirectory(prefix='vim-runtime-receipt-') as fixture:
    def git(*arguments, **kwargs):
        return subprocess.check_output(['git', '-C', fixture, *arguments], **kwargs).decode().strip()

    git('init', '-q')
    git('-c', 'user.name=Runtime Fixture', '-c', 'user.email=fixture@example.invalid',
        'commit', '--allow-empty', '-qm', 'fixture')
    receipt = {'QEMURevision': git('rev-parse', 'HEAD')}
    assert check(receipt, fixture), 'Reuse a clean matching checkout'
    assert not check({'QEMURevision': 'older-build'}, fixture), 'Reject stale runtime'
    assert not check({}, fixture), 'Reject missing source provenance'
    assert not check(receipt, Path(fixture)/'missing'), 'Reject unavailable checkout'
    blob = git('hash-object', '-w', '--stdin', input=b'')
    git('update-index', '--add', '--cacheinfo', '100644', blob, 'unbuilt-source.c')
    assert not check(receipt, fixture), 'Reject dirty source, including staged-only changes'

print('Runtime cache checks pass: clean match, stale/missing provenance, missing checkout, dirty source.')
