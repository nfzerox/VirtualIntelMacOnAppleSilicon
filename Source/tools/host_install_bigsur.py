"""Build a Big Sur VM from original Apple media using native host processes.

Development entry point for the host-assembly pipeline, not the shipped UI.
The guest is not started here. The output is published only after verification.
"""

import argparse
from contextlib import contextmanager
import getpass
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
from uuid import uuid4

from host_install_apfs import attached_gpt, attached_images, create_split_volume_group, recovery_efi_driver, run
from host_install_apfs_checkpoint import write_bundle_blocks
from host_install_apfs_records import APFS, INODE, PLACEHOLDER, SF_FIRMLINK, RegularImage, XATTR, detached_image, u32, u64
from host_install_apfs_seed import format_native_seed, publish_native_jumpstart
from host_install_bigsur_boot import stage_boot_assets
from host_install_bigsur_canonical import canonical_metadata, original_seal_metadata
from host_install_bigsur_data import project_mutable_volumes, stage_data_image
from host_install_bigsur_fixups import compare_archive_compression, original_fixups
from host_install_bigsur_media import inspect_asset, signed_asset
from host_install_bigsur_remap import remapping_dictionary
from host_install_bigsur_seal import extract_seal_inputs
from host_install_bigsur_snapshot import assemble, export_bigsur_machine, stage_boot_selector, verify_detached
from host_install_bigsur_system import finalize_system_volume, stage_system_volume
from host_install_bigsur_tree import export_verified_tree
from host_install_hfs import shadow_hash


@contextmanager
def scratch_authorization():
    """Keep this terminal's already granted scratch authorization during long steps."""
    command = ("/usr/bin/sudo", "-n", "-v")
    run(*command)
    finished = threading.Event()

    def renew():
        while not finished.wait(60):
            try:
                subprocess.run(command, stdin=subprocess.DEVNULL, timeout=10, check=True)
            except (OSError, subprocess.SubprocessError):
                print("Scratch authorization expired; it could not be renewed without prompting.", flush=True)
                return

    thread = threading.Thread(target=renew, daemon=True)
    thread.start()
    try:
        yield
    finally:
        finished.set()
        thread.join()


def recovered_extraction_group(image, archive, work, size):
    # Diagnostic recovery only, for a known failed job interrupted after
    # check-and-fix but before firmlink reservation or detached edits.
    if image != work / "System.dmg" or image.is_symlink() or not image.is_file() or \
            image.stat().st_size != size * 2**30 or attached_images(image):
        raise ValueError("Use the detached, original flat image of the failed scratch job")
    volumes = {volume.role: volume for volume in APFS(detached_image(image)).volumes()}
    system, data = volumes[1], volumes[64]
    if system.sealed or system.snapshot_count or system.superblock[0x3f0:0x400] != data.uuid.bytes or \
            any((kind == XATTR and entry.key[10:] == PLACEHOLDER) or
                (kind == INODE and u32(entry.value, 68) & SF_FIRMLINK)
                for node in system.nodes() if node.leaf for entry in node.entries
                for kind in (u64(entry.key, 0) >> 60,)):
        raise ValueError("Do not remount a Big Sur image after detached finalization has begun")
    compression = compare_archive_compression(system, original_fixups(archive))
    if compression["Differences"]:
        raise ValueError("The recovered Big Sur image did not complete Apple's compression fixups")
    return str(data.uuid).upper()


def prepare_system(image, package, runtime, host_tools, work, account, size, *, recover_extraction=False):
    with signed_asset(package) as (asset, archive):
        inventory = inspect_asset(asset, archive)
        if recover_extraction:
            print("Verifying the detached System from the interrupted scratch job", flush=True)
            group = recovered_extraction_group(image, archive, work, size)
            dependency_work = Path(tempfile.mkdtemp(prefix="RecoveredDependencies-", dir=work))
        else:
            print("Creating the native APFS volume group", flush=True)
            format_native_seed(image, size, flat=True)
            with attached_gpt(image) as (whole, _):
                _, group = create_split_volume_group(whole, include_update=True)
            dependency_work = work
        owner = {"FormatVersion": 1, "Kind": "VirtualIntelMacLegacySetup", "Guest": "bigsur",
                 "Build": inventory["Build"], "MachineIdentifier": str(uuid4()).upper(), "VolumeUUID": group,
                 "Enabled": True, "AutomaticLogin": account[3], "RemoteLogin": account[4], "FileSharing": False}
        boot_assets = dependency_work / "BootAssets"

        def stage_dependencies(root):
            print("Preparing the account, graphics and original boot assets", flush=True)
            derived["Data"] = stage_data_image(root, archive, runtime, dependency_work, owner, account, host_tools=host_tools)
            derived["RootHash"] = stage_boot_assets(archive, inventory, root, boot_assets, group)

        derived = {}
        staged = stage_system_volume(image, archive, inventory, group, with_system=stage_dependencies,
                                     decoder=host_tools / "host-install-pbzx",
                                     already_extracted=recover_extraction)
        project_mutable_volumes(image, derived["Data"], boot_assets, group)
        snapshot = uuid4()
        stage_boot_selector(image, snapshot, work / "root_uuid", None)
        # No host mount of the candidate after this point.
        finalize_system_volume(image, archive, staged)
        print("Rebuilding Apple's signed System metadata tree", flush=True)
        mtree, digest = extract_seal_inputs(archive, work / "SealInputs",
                                            decoder=host_tools / "host-install-pbzx")
        volume, = (v for v in APFS(detached_image(image)).volumes() if v.role == 1)
        with mtree.open("rb") as manifest:
            remap, _ = remapping_dictionary(volume, manifest)
        stamp = remap.pop("CHANGE")
        remap = {int(source): target for source, target in remap.items()}
        policy = original_seal_metadata(archive, volume)

    tree = work / "SealTree"
    expected = derived["RootHash"]
    with subprocess.Popen((host_tools / "host-install-bigsur-digest", digest), stdout=subprocess.PIPE) as reader:
        with reader.stdout:
            export_verified_tree(tree, canonical_metadata(volume, remap, stamp, policy), reader.stdout, expected)
        if reader.wait():
            raise ValueError("Could not read Apple's original Big Sur digest database")
    base = boot_assets / "Recovery" / group / "BaseSystem.dmg"
    raw = work / "Recovery.raw"
    run(runtime / "bin/qemu-img", "convert", "-f", "dmg", "-O", "raw", base, raw)
    with raw.open("rb") as source:
        driver = recovery_efi_driver(RegularImage(source))
    raw.unlink()
    print("Publishing the Intel EFI driver and authenticated snapshot in the detached candidate", flush=True)
    container = publish_native_jumpstart(image, driver)
    write_bundle_blocks(container.image, container.offset, assemble(container, tree, expected, remap, snapshot))
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--media", type=Path, required=True, help="Original Apple-signed Big Sur InstallAssistant.pkg")
    parser.add_argument("--app", type=Path, required=True, help="App providing the Big Sur support and QEMU runtime")
    parser.add_argument("--host-tools", type=Path, required=True, help="Native graphics and seal helpers")
    parser.add_argument("--output", type=Path, required=True, help="New VM bundle on the scratch drive")
    parser.add_argument("--username", required=True)
    parser.add_argument("--full-name")
    parser.add_argument("--auto-login", action="store_true")
    parser.add_argument("--ssh", action="store_true")
    parser.add_argument("--ssh-port", type=int, default=23227)
    parser.add_argument("--size", type=int, default=32)
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error("Run as a regular user with separate approval for scratch volume metadata")
    if not re.fullmatch(r"[a-z][a-z0-9_-]{0,30}", args.username):
        parser.error("Choose a valid short account name")
    if not 32 <= args.size <= 2048 or not 1024 <= args.ssh_port <= 65535:
        parser.error("Choose a disk of 32–2048 GiB and an unprivileged TCP port")
    output = args.output.absolute()
    if output.suffix != ".vintelmac" or output.exists() or output.is_symlink():
        parser.error("Choose a new .vintelmac output path")
    media, app, tools = (p.resolve(strict=True) for p in (args.media, args.app, args.host_tools))
    executables = [tools / name for name in ("guest-prepare", "guest-macho", "guest-image-patch",
                                            "host-install-bigsur-digest", "host-install-pbzx")]
    runtime = app / "Contents/Resources/Runtime"
    for tool in (*executables, runtime / "bin/qemu-img"):
        archs = subprocess.check_output(("/usr/bin/lipo", "-archs", tool), text=True).split()
        if not {"arm64", "x86_64"} <= set(archs):
            parser.error(f"{tool} must be a universal host executable")
    password = getpass.getpass("New guest account password: ").encode("utf-8")
    shadow_hash(password)
    work = Path(tempfile.mkdtemp(prefix=".vim-bigsur-native-", dir=output.parent))
    print(f"Scratch: {work}", flush=True)
    image = work / "System.dmg"
    account = (args.username, args.full_name or args.username, password, args.auto_login, args.ssh)
    with scratch_authorization():
        expected = prepare_system(image, media, runtime, tools, work, account, args.size)
    print("Checking the final detached APFS container", flush=True)
    verify_detached(APFS(detached_image(image)), {})
    print("Exporting the standalone VM disk", flush=True)
    export_bigsur_machine(image, app, output, expected, args.ssh_port)


if __name__ == "__main__":
    main()
