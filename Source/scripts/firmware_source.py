"""Offline, pinned EDK II source input for the optional firmware rebuild."""
import hashlib
from pathlib import Path
import plistlib
import subprocess
import tarfile

EDK_REVISION = '2970e5699ba6267f3384ffab20f96647578aebc8'
EDK_SOURCE = 'https://github.com/tianocore/edk2.git'
SUBMODULES = (
    'CryptoPkg/Library/OpensslLib/openssl',
    'CryptoPkg/Library/MbedTlsLib/mbedtls',
    'SecurityPkg/DeviceSecurity/SpdmLib/libspdm',
    'MdeModulePkg/Universal/RegularExpressionDxe/oniguruma',
    'TcgTpmPkg/Library/TpmLib/TPM',
    'BaseTools/Source/C/BrotliCompress/brotli',
    'MdeModulePkg/Library/BrotliCustomDecompressLib/brotli',
    'MdePkg/Library/MipiSysTLib/mipisyst',
    'MdePkg/Library/BaseFdtLib/libfdt',
)


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def checkout_receipt(root):
    """Explicit maintenance override: verify, never fetch or mutate checkouts."""
    revision = git(root, 'rev-parse', 'HEAD')
    if revision != EDK_REVISION or git(root, 'status', '--porcelain', '--untracked-files=all'):
        raise RuntimeError('EDK II must be the pristine pinned revision.')
    children = {}
    for relative in SUBMODULES:
        fields = git(root, 'ls-tree', 'HEAD', relative).split()
        if len(fields) != 4 or fields[0] != '160000':
            raise RuntimeError('Missing pinned firmware dependency: '+relative)
        child = root/relative
        # rev-parse alone walks to a parent repository if a child is absent.
        if not (child/'.git').exists() or git(child, 'rev-parse', 'HEAD') != fields[2]:
            raise RuntimeError('Materialize the pinned firmware dependency first: '+relative)
        if git(child, 'status', '--porcelain', '--untracked-files=all'):
            raise RuntimeError('Modified firmware dependency: '+relative)
        children[relative] = {'Revision': fields[2], 'URL': git(
            root, 'config', '-f', '.gitmodules', '--get', 'submodule.'+relative+'.url')}
    return {'Revision': revision, 'URL': EDK_SOURCE, 'Dependencies': children}


def archive_input(source):
    archive = source/'vendor/edk2-source.tar.gz'
    manifest = source/'vendor/edk2-source.sha256'
    fields = manifest.read_text().split()
    if len(fields) != 2 or fields[1] != archive.name or len(fields[0]) != 64:
        raise RuntimeError('Missing or ambiguous firmware source checksum')
    with archive.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual != fields[0]:
        raise RuntimeError('Vendored firmware source checksum mismatch')
    return archive, actual


def extract(source, destination):
    """Caller holds its work-directory lock. Refuse uncertain derived trees."""
    if not hasattr(tarfile, 'data_filter'):
        raise RuntimeError('Firmware extraction requires Python 3.12 or newer.')
    archive, checksum = archive_input(source)
    stamp = destination/'.VIMArchiveSHA256'
    if destination.exists():
        if not stamp.is_file() or stamp.read_text() != checksum:
            raise RuntimeError('Unrecognized firmware work tree; use a new --work directory.')
    else:
        destination.mkdir()
        # Checksum is verified before extraction. The data filter additionally
        # rejects special files and traversal/symlinks outside this new tree.
        with tarfile.open(archive, 'r:gz') as stream:
            stream.extractall(destination, filter='data')
        stamp.write_text(checksum)
    receipt = plistlib.loads((destination/'VIMSourceReceipt.plist').read_bytes())
    if receipt.get('Revision') != EDK_REVISION or set(receipt.get('Dependencies', {})) != set(SUBMODULES):
        raise RuntimeError('Firmware source receipt does not match the pinned recipe')
    return {**receipt, 'ArchiveSHA256': checksum}
