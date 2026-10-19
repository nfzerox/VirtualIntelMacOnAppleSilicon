"""One persistent host identity for packaged and UI-only builds."""
import os
from pathlib import Path
import plistlib
import subprocess
import sys


def resolve_identity(source, requested=None):
    source = Path(source)
    shared = source/'Private/DevelopmentSigning/Identity.plist'
    requested = requested or os.environ.get('VIM_CODESIGN_IDENTITY')
    if shared.is_file():
        identity = plistlib.loads(shared.read_bytes())['Identity']
        if requested and requested.upper() != identity.upper():
            raise RuntimeError('Private-repository builds must use the checked-in shared signing identity. '
                               'Remove the conflicting signing override; the key is restored automatically.')
        # Also repairs a missing key on a new builder. Never create a new
        # private identity just because this machine has not built before.
        subprocess.run([sys.executable, str(source/'scripts/setup-development-signing.py')], check=True)
        return identity
    if requested:
        if requested == '-':
            raise RuntimeError('Host builds require a persistent signing identity, not ad-hoc signing.')
        return requested
    # Exported source has no private key. Give that builder a persistent local
    # identity instead, shared by Debug, Release and fast UI iterations.
    subprocess.run([sys.executable, str(source/'scripts/setup-development-signing.py')], check=True)
    return plistlib.loads((source/'Build/DevelopmentSigning.plist').read_bytes())['Identity']
