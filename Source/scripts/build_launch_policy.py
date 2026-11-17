"""Keep native 11.0 compatibility while qualifying bundle launches on 15+."""
from pathlib import Path
import plistlib


LAUNCH_MINIMUM = "15.0"
BINARY_MINIMUM = "11.0"


def apply_launch_minimum(app):
    """Call before the final app signature, never patch Mach-O deployment data."""
    path = Path(app) / "Contents/Info.plist"
    original = path.read_bytes()
    info = plistlib.loads(original)
    info["LSMinimumSystemVersion"] = LAUNCH_MINIMUM
    info.pop("LSMinimumSystemVersionByArchitecture", None)
    path.write_bytes(plistlib.dumps(info, sort_keys=False,
        fmt=plistlib.FMT_BINARY if original.startswith(b"bplist") else plistlib.FMT_XML))
