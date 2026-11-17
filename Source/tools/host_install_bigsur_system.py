"""Populate Big Sur's unpublished System volume from authenticated media."""

import json
from pathlib import Path
import stat
import subprocess
from uuid import UUID

from host_install_apfs import attached_gpt, attached_images, mounted_volume, verify_split_layout
from host_install_apfs_records import APFS, BLOCK, PLACEHOLDER, SF_FIRMLINK, detached_image, finalize_firmlinks
from host_install_bigsur_fixups import compare_archive_compression, fixup_blocks, original_fixups, plan_fixups
from host_install_bigsur_media import PAYLOAD, apply_main_chunks, run_archive, verify_main_payload
from host_install_catalina import parse_firmlinks
from host_install_hfs import mount_points, run


SUDO = ("/usr/bin/sudo", "-n")
FIRMLINKS = Path("usr/share/firmlinks")


def firmlink_layout(archive, root):
    pairs = parse_firmlinks((root / FIRMLINKS).read_text())
    metadata = json.loads(subprocess.check_output(
        ("/usr/bin/aa", "list", "-list-format", "json"), input=archive.read(PAYLOAD + "firmlinks_payload")))
    stubs = {}
    for row in metadata:
        relative = Path(row["PAT"])
        if relative not in pairs or relative in stubs or row["TYP"] != "D" or not row["FLG"] & SF_FIRMLINK:
            raise ValueError("Unexpected directory in the signed Big Sur firmlink payload")
        stubs[relative] = (pairs[relative], row["UID"], row["GID"], row["MOD"])
    if stubs.keys() != pairs.keys():
        raise ValueError("Big Sur's archive and System firmlink lists disagree")
    return stubs


def reserve_firmlinks(archive, root):
    reserved = {}
    for relative, (destination, uid, gid, mode) in firmlink_layout(archive, root).items():
        stub = root / relative
        if not stub.parent.is_dir() or stub.is_symlink() or stub.exists():
            raise ValueError(f"Expected an unused Big Sur firmlink location: {relative}")
        run(*SUDO, "/bin/mkdir", "-m", f"{mode:o}", stub)
        run(*SUDO, "/usr/sbin/chown", f"{uid}:{gid}", stub)
        info = stub.stat()
        if (info.st_uid, info.st_gid, stat.S_IMODE(info.st_mode)) != (uid, gid, mode):
            raise ValueError(f"Big Sur firmlink directory metadata was not preserved: {relative}")
        value = destination.as_posix().encode() + b"\0"
        # A placeholder reserves the ordinary inode/xattr records while the
        # scratch disk is mounted. Finalization touches only its detached copy.
        run(*SUDO, "/usr/bin/xattr", "-wx", PLACEHOLDER[:-1].decode(), value.hex(), stub)
        reserved[info.st_ino] = value
    return reserved


def restore_archive_compression(archive, root, *, decoder=None):
    """Replay Apple's filesystem compression choices, not the host default."""
    # Only the original files. Directories, signed firmlink flags, ACLs,
    # ownership and modes are handled by extraction and detached fixups.
    try:
        options = {"decoder": decoder} if decoder is not None else {}
        run_archive(archive, PAYLOAD + "fixup.manifest", "check-and-fix", "-d", root, "-include-type", "f",
                    "-exclude-field", "uid,gid,mod,acl,flg", "-v", authorize=True, **options)
    except subprocess.CalledProcessError as error:
        # aa may return 1 after repairing all compression representations.
        # Only the detached verification below can accept that result.
        if error.returncode != 1:
            raise
        return error.returncode
    return 0


def finish_system_metadata(image_path, system_uuid, rows):
    if attached_images(image_path):
        raise ValueError("Detach Big Sur before final System metadata verification")
    image = detached_image(image_path)
    container = APFS(image)
    expected = UUID(system_uuid)
    volume, = (v for v in container.volumes() if v.role == 1 and v.uuid == expected)
    compression = compare_archive_compression(volume, rows)
    if compression["Differences"]:
        raise ValueError(f"Big Sur compression readback disagrees with Apple's manifest: {compression['Differences']}")
    _, edits = plan_fixups(volume, rows)
    if edits:
        blocks = fixup_blocks(volume, edits)
        if attached_images(image_path):
            raise ValueError("Big Sur was attached while preparing final metadata")
        for address, block in blocks.items():
            image.write(container.offset + BLOCK * address, block)
        volume, = (v for v in APFS(image).volumes() if v.role == 1 and v.uuid == expected)
        if plan_fixups(volume, rows)[1]:
            raise ValueError("Big Sur metadata readback does not match Apple's manifest")


def stage_system_volume(image, archive, inventory, group_uuid, *, with_system=None, decoder=None,
                        already_extracted=False):
    """Extract System; reserve metadata slots without editing APFS blocks yet."""
    with attached_gpt(image) as (whole, _), mount_points() as (_, root):
        volumes, observed = verify_split_layout(whole + "s2", include_update=True)
        if observed != str(UUID(str(group_uuid))).upper():
            raise ValueError("Big Sur System does not belong to the new APFS group")
        system = volumes["Macintosh HD"]
        with mounted_volume(system, root, authorize=True, owners=True):
            compression_status = 0
            if not already_extracted:
                if (root / "System").exists():
                    raise ValueError("Big Sur System projection requires a new empty volume")
                apply_main_chunks(archive, root, inventory["Chunks"], preserve_metadata=True, decoder=decoder)
                compression_status = restore_archive_compression(archive, root, decoder=decoder)
            verify_main_payload(root, inventory)
            if with_system is not None:
                with_system(root)
            reserved = reserve_firmlinks(archive, root)
    return system["APFSVolumeUUID"], reserved, compression_status


def finalize_system_volume(image, archive, staged):
    """Only after all mount-based writes are complete: restore original metadata."""
    system_uuid, reserved, compression_status = staged
    finalize_firmlinks(image, system_uuid, reserved)
    finish_system_metadata(image, system_uuid, original_fixups(archive))
    if compression_status:
        print("Verified Apple Archive compression despite its nonzero comparison status", flush=True)
    return len(reserved)


def project_system_volume(image, archive, inventory, group_uuid):
    """Install the original System tree and firmlinks, but do not bless or seal."""
    return finalize_system_volume(image, archive, stage_system_volume(image, archive, inventory, group_uuid))
