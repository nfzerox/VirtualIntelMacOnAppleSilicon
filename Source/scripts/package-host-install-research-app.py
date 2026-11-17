#!/usr/bin/env python3
"""Build only the universal host installer into a NEW, non-release app copy.

The reference app is never modified. The real Create flow and unchanged
QEMU/guest payloads remain in use. Release verification rejects this package;
use the normal build-app command after component qualification.
"""
import argparse
import importlib.util
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    reference = args.reference_app.resolve(strict=True)
    if args.output.exists() or args.output.suffix != ".app":
        parser.error("Choose a new .app output path; existing apps are never replaced.")
    worker = Path("Contents/Resources/Runtime/HostInstall/install-worker")
    if not (reference / worker).is_file():
        parser.error("The reference must be a packaged app with its native installer.")
    sys.path.insert(0, str(source / "scripts"))
    from build_signing import resolve_identity
    identity = resolve_identity(source, None)
    spec = importlib.util.spec_from_file_location("runtime_packager", source / "scripts/package-app-runtime.py")
    recipe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recipe)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(reference, args.output, symlinks=True)
    recipe.build_host_install_worker(source, args.output / worker, identity)
    marker = args.output / "Contents/Resources/Runtime/ResearchRuntime.plist"
    marker.write_bytes(plistlib.dumps({"ResearchOnly": True, "ProductionQualification": False,
        "Component": "HostInstall", "ReferenceRuntime": str(reference),
        "SourceCommit": subprocess.check_output(("git", "-C", source, "rev-parse", "HEAD"), text=True).strip()}))
    subprocess.run(("codesign", "--force", "--sign", identity, args.output), check=True)
    subprocess.run(("codesign", "--verify", "--deep", "--strict", args.output), check=True)
    print(f"Non-release host-install component app: {args.output}")


if __name__ == "__main__":
    main()
