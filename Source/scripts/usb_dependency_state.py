"""Small input receipt shared by runtime and USB-helper builders."""
import hashlib
from pathlib import Path
import subprocess


def usb_inputs_hash(source):
    source = Path(source).resolve()
    inputs = [source/'scripts/build-universal-dependencies.py', source/'scripts/deployment_target.py', Path(__file__).resolve(),
              source/'vendor/qemu-dependency-sources.sha256']
    inputs += sorted(p for p in (source/'vendor/usbredir').rglob('*') if p.is_file())
    inputs += sorted((source/'vendor/patches').glob('libusb-*.patch'))
    digest = hashlib.sha256()
    for path in inputs:
        digest.update(path.relative_to(source).as_posix().encode()+b'\0')
        digest.update(path.read_bytes())
    return digest.hexdigest()


def apply_libusb_patches(source, extracted):
    """Only a derived source copy is changed; reject unknown/fuzzy input."""
    for patch in sorted((Path(source)/'vendor/patches').glob('libusb-*.patch')):
        command = ['/usr/bin/patch', '-f', '-d', str(Path(extracted)/'libusb'),
                   '-p1', '-F0', '-s', '-i', str(patch.resolve())]
        # -C is macOS patch's check-only mode. An already-applied exact patch
        # is accepted, without another edit, backup or reject file.
        if subprocess.run([*command, '-C'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0:
            subprocess.run(command, stdin=subprocess.DEVNULL, check=True)
        elif subprocess.run([*command, '-R', '-C'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL).returncode:
            raise RuntimeError(f'libusb source does not match the pinned patch: {patch.name}')
