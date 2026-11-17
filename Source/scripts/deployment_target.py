"""Keep configure results honest about the macOS deployment target.

Meson's has_function checks declare their own prototype, so an SDK function
that only exists on a newer macOS than the deployment target is still found.
The binary then weak-imports it and calls NULL on older hosts (for example
pipe2 and dup3, which the macOS 27 SDK declares as macOS 27.0+).

undefine_unavailable() removes such HAVE_/CONFIG_ entries from a generated
config header, so the code takes its existing fallback path.
check_weak_libsystem_imports() refuses binaries that still weak-import libc
functions, catching anything the header pass cannot see.
"""
import re
import os
from contextlib import contextmanager
from functools import lru_cache
import subprocess
import tempfile
from pathlib import Path

HEADERS = ('unistd.h', 'stdlib.h', 'string.h', 'stdio.h', 'fcntl.h', 'poll.h', 'time.h',
           'signal.h', 'spawn.h', 'pthread.h', 'sched.h', 'dirent.h', 'locale.h', 'xlocale.h',
           'netdb.h', 'sys/socket.h', 'sys/stat.h', 'sys/time.h', 'sys/uio.h', 'sys/mman.h',
           'sys/random.h', 'sys/event.h', 'sys/select.h', 'sys/mount.h', 'sys/sysctl.h')
# Weak imports known to exist on every supported host: Apple's runtime
# availability helper, the stack probe present since macOS 10.15, and libusb's
# optional dispatch_once_f fast path (SDK dispatch/once.h: macOS 10.6).
# Swift's macOS 11 back-deployment archives weak-reference the libdispatch
# object/voucher ABI. Both exports predate Big Sur (also present in our 10.14
# SDK's libdispatch.tbd); os/object.h declares os_release as macOS 10.10+.
# Swift back-deployment code can also weak-reference the public unfair-lock
# trio. os/lock.h declares these macOS 10.12+, and the genuine Big Sur SDK's
# libsystem_platform.tbd exports all three on both supported architectures.
ALLOWED_WEAK = {'__availability_version_check', '___chkstk_darwin', '_dispatch_once_f',
                '_os_release', '_voucher_adopt', '_os_unfair_lock_lock',
                '_os_unfair_lock_trylock', '_os_unfair_lock_unlock'}


def _flags(arch, minimum):
    return ['xcrun', 'clang', '-arch', arch, f'-mmacosx-version-min={minimum}', '-fsyntax-only',
            '-Werror=unguarded-availability', '-Werror=unguarded-availability-new', '-x', 'c', '-']


@lru_cache(maxsize=8192)
def _unavailable_function(name, arch, minimum):
    # One build process has one selected SDK/toolchain. Repeated config-header
    # checks before/after Ninja and across dependencies must not launch Clang
    # hundreds of times for the exact same name/architecture/minimum tuple.
    # No persistent cache can outlive an SDK change or suppress a later check.
    prelude = ''.join(f'#include <{h}>\n' for h in HEADERS)
    source = prelude + f'void *vim_probe(void) {{ return (void *)&{name}; }}\n'
    result = subprocess.run(_flags(arch, minimum), input=source, capture_output=True, text=True)
    return bool(result.returncode and 'only available on macOS' in result.stderr)


def unavailable_functions(names, arch, minimum):
    """Names that the SDK declares as functions introduced after `minimum`."""
    found = []
    for name in sorted(set(names)):
        if not re.fullmatch(r'[a-z_][a-z0-9_]*', name):
            continue
        if _unavailable_function(name, arch, minimum):
            found.append(name)
    return found


def undefine_unavailable(config_header, prefix, arch, minimum='11.0'):
    """Rewrite `#define PREFIXNAME` entries for too-new functions as #undef."""
    path = Path(config_header)
    text = path.read_text()
    pattern = re.compile(r'^#define (' + re.escape(prefix) + r'([A-Z0-9_]+))(?:\s.*)?$', re.M)
    candidates = {m.group(2).lower(): m.group(1) for m in pattern.finditer(text)}
    removed = unavailable_functions(candidates, arch, minimum)
    if removed:
        for name in removed:
            macro = candidates[name]
            text = re.sub(r'^#define ' + re.escape(macro) + r'(?:\s.*)?$',
                          f'#undef {macro} /* {name}: newer than macOS {minimum} */', text, flags=re.M)
        path.write_text(text)
    return removed


@contextmanager
def preserve_unchanged_config_timestamp(config_header):
    """Avoid rebuilding every consumer when configure plus SDK filtering is a no-op.

    Configure can put too-new functions back before undefine_unavailable removes
    them again. Compare the final bytes, not just the two individual writes.
    Real header/command/source changes remain visible to Ninja as usual.
    """
    path = Path(config_header)
    previous = (path.read_bytes(), path.stat().st_mtime_ns) if path.is_file() else None
    yield
    if previous is not None and path.is_file() and path.read_bytes() == previous[0]:
        os.utime(path, ns=(path.stat().st_atime_ns, previous[1]))


def _weak_libsystem_imports(binary, arch, *, historical_guest=False):
    command = ['xcrun', 'dyld_info', '-arch', arch, '-imports', str(binary)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode == 0:
        pattern = r'\s+(\S+) \[weak-import\] \(from libSystem\)'
        lines = result.stdout
    elif historical_guest and 'mis-aligned LINKEDIT string pool' in result.stdout + result.stderr:
        # Period ld/codesign binaries can have a non-8-byte-aligned string
        # table accepted by their guest dyld. Read N_WEAK_REF from the
        # undefined symbol table instead of relaxing the import check.
        lines = subprocess.check_output(['xcrun', 'nm', '-arch', arch, '-m', '-u', str(binary)], text=True)
        pattern = r'\s+\(undefined\) weak external (\S+) \(from libSystem\)'
    else:
        raise RuntimeError(f'{binary} ({arch}): {result.stderr.strip() or result.stdout.strip()}')
    return [match.group(1) for line in lines.splitlines() if (match := re.match(pattern, line))]


def check_weak_libsystem_imports(binary, *, historical_guest=False):
    """Raise if a binary weak-imports libSystem symbols beyond the allowlist."""
    weak = []
    for arch in subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split():
        for name in _weak_libsystem_imports(binary, arch, historical_guest=historical_guest):
            if name not in ALLOWED_WEAK:
                weak.append(f'{arch}:{name}')
    if weak:
        raise RuntimeError(f'{binary} weak-imports libSystem functions that may be missing on older macOS: '
                           + ', '.join(weak))
