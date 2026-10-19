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
ALLOWED_WEAK = {'__availability_version_check', '___chkstk_darwin', '_dispatch_once_f'}


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


def check_weak_libsystem_imports(binary):
    """Raise if a binary weak-imports libSystem symbols beyond the allowlist."""
    weak = []
    for arch in subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split():
        imports = subprocess.check_output(['xcrun', 'dyld_info', '-arch', arch, '-imports', str(binary)], text=True)
        for line in imports.splitlines():
            match = re.match(r'\s+(\S+) \[weak-import\] \(from libSystem\)', line)
            if match and match.group(1).lstrip('_') not in {s.lstrip('_') for s in ALLOWED_WEAK}:
                weak.append(f'{arch}:{match.group(1)}')
    if weak:
        raise RuntimeError(f'{binary} weak-imports libSystem functions that may be missing on older macOS: '
                           + ', '.join(weak))
