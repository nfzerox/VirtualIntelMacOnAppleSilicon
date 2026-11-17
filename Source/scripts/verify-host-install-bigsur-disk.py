#!/usr/bin/env python3
"""Qualify a detached Big Sur disk against the original signed Intel installer."""

import argparse
from contextlib import contextmanager
from hashlib import sha256
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs import attached_images, read_efi_jumpstart, recovery_efi_driver
from host_install_apfs_records import APFS, BLOCK, MappedQCOWImage, detached_image
from host_install_bigsur_file_storage import verify_sealed_file_data
from host_install_bigsur_snapshot import media_digest, verify_bigsur_boot_files, verify_detached


@contextmanager
def open_disk(path, qemu_img):
    if qemu_img is None:
        disk = detached_image(path)
        try:
            yield disk
        finally:
            disk.close()
        return

    def stamp(info):
        return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns

    with open(path, "rb", opener=lambda filename, flags: os.open(filename, flags | os.O_NOFOLLOW)) as stream:
        original = stamp(os.fstat(stream.fileno()))
        # QEMU's normal image lock refuses a disk in use. Do not force share
        # or resolve data from a backing file; shut the guest down first.
        info = json.loads(subprocess.check_output((qemu_img, "info", "--output=json", "-f", "qcow2", path)))
        if (info.get("format") != "qcow2" or info.get("backing-filename") or info.get("full-backing-filename") or
                info.get("dirty-flag") or info.get("format-specific", {}).get("data", {}).get("data-file")):
            raise ValueError("Use a clean, stopped, standalone QCOW2 disk")
        disk = MappedQCOWImage.from_qemu(stream, qemu_img)
        if disk.size != info.get("virtual-size"):
            raise ValueError("QCOW2 allocation map and virtual size disagree")
        yield disk
        if original != stamp(os.fstat(stream.fileno())) or original != stamp(path.lstat()):
            raise ValueError("The QCOW2 disk changed during qualification")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disk", type=Path, help="Detached regular raw GPT disk or a stopped standalone QCOW2")
    parser.add_argument("installer", type=Path, help="Original Apple-signed Big Sur package")
    parser.add_argument("--qemu-img", type=Path, help="Use the app's qemu-img when verifying the final QCOW2")
    parser.add_argument("--all-file-data", action="store_true", help="Also verify every signed System data stream, not just the boot caches")
    parser.add_argument("--software-keybags", action="store_true",
                        help="Decode Big Sur's unencrypted-volume keybags for a strict check of the disposable APFS copy")
    parser.add_argument("--recovery-raw", type=Path, required=True, help="Raw conversion of original Recovery BaseSystem")
    parser.add_argument("--username", required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    if args.evidence.exists() or args.evidence.is_symlink():
        parser.error("Choose a new evidence file")
    for source in (args.disk, args.recovery_raw):
        if not stat.S_ISREG(source.lstat().st_mode) or attached_images(source):
            parser.error("Both disk inputs must be detached regular files")
    expected = media_digest(args.installer)
    with open_disk(args.disk, args.qemu_img) as disk, open_disk(args.recovery_raw, None) as recovery:
        container = APFS(disk)
        account, identity, group, snapshot = verify_bigsur_boot_files(container, expected)
        if account.get("Username") != args.username or not account.get("FirstLoginPolicyPrepared"):
            raise ValueError("The requested account or first-login policy was not prepared")
        start = container.offset // 512
        end = start + container.blocks * BLOCK // 512 - 1
        driver = read_efi_jumpstart(disk, start, end)
        if driver != recovery_efi_driver(recovery):
            raise ValueError("Jumpstart does not match the Intel driver in the original Recovery image")
        file_proof = {}
        if args.all_file_data:
            system, = (volume for volume in container.volumes() if volume.role == 1)
            file_proof = verify_sealed_file_data(system, expected)
        # The OS checker sees only a copied ordinary file, never a guest block
        # device. The owned checker workspace stays on the library's volume.
        decoded = verify_detached(container, {}, software_keybags=args.software_keybags)
    proof = {"Build": account["Build"], "MachineIdentifier": str(identity).upper(),
             "VolumeGroupUUID": group, "BootSnapshotUUID": snapshot, "RootSHA256": expected.hex(),
             "EFIJumpstartSHA256": sha256(driver).hexdigest(), "StockReadOnlyAPFSCheck": True,
             "SoftwareDecodedKeybagObjects": decoded,
             "IntelDyldCachesVerified": ["x86_64", "x86_64h"], "DiskFormat": "qcow2" if args.qemu_img else "raw",
             **file_proof}
    with args.evidence.open("x") as out:
        json.dump(proof, out, indent=2)
        out.write("\n")
    print(f"Detached Big Sur disk passed seal, cache, boot and stock APFS checks: {args.evidence}")


if __name__ == "__main__":
    main()
