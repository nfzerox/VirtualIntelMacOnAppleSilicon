"""Layout of an app's newly generated, read-only firmware resources."""
import filecmp
from pathlib import Path
import shutil


def share_identical_code(firmware):
    """Share matching OVMF code, retaining any genuinely different image.

    Only call inside a new packaging output, before signing the app. Guest
    NVRAM is never linked: each VM still receives its own writable seed copy.
    No byte patches, hashes, guest migrations or aliases are needed. The app
    loads Common/CODE.fd directly; per-OS files remain only for real variants.
    """
    firmware = Path(firmware)
    candidates = sorted(p for p in firmware.glob('*/CODE.fd')
                        if p.parent.name != 'Common' and p.is_file() and not p.is_symlink())
    if len(candidates) < 2:
        return 0
    reference = candidates[0]
    identical = [p for p in candidates if filecmp.cmp(reference, p, shallow=False)]
    if len(identical) < 2:
        return 0
    shared = firmware/'Common/CODE.fd'
    shared.parent.mkdir(exist_ok=True)
    if not shared.exists():
        shutil.copy2(reference, shared)
    elif not filecmp.cmp(reference, shared, shallow=False):
        raise RuntimeError('The generated common firmware differs from its source.')
    saved = (len(identical)-1)*shared.stat().st_size
    for path in identical:
        path.unlink()
    return saved
