"""Prepare Big Sur's mutable files without writing to the System volume."""

import json
from pathlib import Path
import stat
import tempfile
from uuid import UUID

from host_install_bigsur_media import PAYLOAD, run_archive
from host_install_apfs import DISKUTIL, attached_gpt, mounted_volume, plist_command, verify_split_layout
from host_install_hfs import DITTO, HDI, Target, attach, catalog_permissions, mount_points, restore_metadata, run, stage_account
from host_install_mojave import prepared_pvg_payload


TEMPLATE = Path("System/Library/Templates/Data")
GRAPHICS_TARGETS = Path(__file__).resolve().parents[1] / "support/install-targets-bigsur.txt"


def original_data_metadata(archive):
    with tempfile.TemporaryFile() as stream:
        run_archive(archive, PAYLOAD + "fixup.manifest", "list", "-list-format", "json",
                    "-include-path", TEMPLATE.as_posix(), output=stream)
        stream.seek(0)
        rows = json.load(stream)
    kinds = {"D": stat.S_IFDIR, "F": stat.S_IFREG, "L": stat.S_IFLNK, "H": stat.S_IFREG}
    metadata = {}
    for row in rows:
        relative = Path(row["PAT"]).relative_to(TEMPLATE)
        if relative == Path("."):
            continue
        if ".." in relative.parts or relative in metadata:
            raise ValueError("Invalid path in the Big Sur Data template manifest")
        metadata[relative] = row["UID"], row["GID"], kinds[row["TYP"]] | row["MOD"]
    return metadata


def copy_data_template(system, target, *, authorized=False):
    # Apple's original template has a 0600 directory, so an ordinary host
    # user can't read every descendant on the read-only System staging mount.
    # The approved root copy is restricted to these two generated mounts.
    prefix = ("/usr/bin/sudo", "-n") if authorized else ()
    run(*prefix, *DITTO, system / TEMPLATE, target.root)


def stage_data_files(target, system, archive, runtime, stage, owner, account, *, authorized=False, host_tools=None):
    target.metadata.update(original_data_metadata(archive))
    copy_data_template(system, target, authorized=authorized)
    name, full_name, password, automatic_login, ssh = account
    stage_account(target, name, full_name, password, owner, automatic_login, ssh, system_root=system)
    with prepared_pvg_payload(system, runtime, stage, owner["Build"], "bigsur", host_tools=host_tools) as payload:
        for name in GRAPHICS_TARGETS.read_text().splitlines():
            relative = Path(name.removeprefix("/"))
            target.mkdir(relative.parent)
            if (target.root / relative).exists():
                raise ValueError(f"Refusing to replace Apple's stock Data entry: {relative}")
            run(*DITTO, payload / relative, target.root / relative)
            target.remember_tree(relative, 0, 0, replacement=True)


def stage_data_image(system, archive, runtime, stage, owner, account, *, host_tools=None):
    """Own a new staging disk; apply Data metadata before final APFS copying."""
    for name in ("Disks", "Logs"):
        (stage / name).mkdir(mode=0o700)
    image = stage / "Disks/BigSurData.sparsebundle"
    run(HDI, "create", "-size", "4g", "-type", "SPARSEBUNDLE", "-layout", "GPTSPUD", "-fs",
        "Journaled HFS+", "-volname", "Big Sur Data Staging", image)
    with mount_points() as (_, root):
        attach("-owners", "off", "-mountpoint", root, image)
        try:
            disk = plist_command(DISKUTIL, "info", "-plist", root)
            target = Target(root, {})
            stage_data_files(target, system, archive, runtime, stage, owner, account, authorized=True,
                             host_tools=host_tools)
            permissions = catalog_permissions(root, target.metadata)
        finally:
            run(HDI, "detach", root)
    restore_metadata(image, disk["PartitionMapPartitionOffset"], disk["Size"], permissions)
    return image


def project_mutable_volumes(image, data_image, boot_assets, group_uuid):
    """Populate only Data, Preboot and Recovery on a new unpublished seed."""
    group = str(UUID(str(group_uuid))).upper()
    root_uuid = Path("System/Library/CoreServices/.root_uuid")
    if (boot_assets / "Preboot" / group / root_uuid).exists():
        raise ValueError("Big Sur boot assets already advertise a System snapshot")
    for volume, file in (("Preboot", "System/Library/CoreServices/boot.efi"), ("Recovery", "BaseSystem.dmg")):
        if not (boot_assets / volume / group / file).is_file():
            raise ValueError(f"Missing staged Big Sur {volume} files")
    sudo = ("/usr/bin/sudo", "-n")
    install = (*sudo, "/usr/bin/install")
    with attached_gpt(image) as (whole, _), mount_points() as (source, target):
        volumes, observed = verify_split_layout(whole + "s2", include_update=True)
        if observed != group or volumes["Macintosh HD - Data"]["APFSVolumeUUID"] != group:
            raise ValueError("Big Sur Data and boot assets must share the new volume group's identity")
        attach("-readonly", "-owners", "on", "-mountpoint", source, data_image)
        try:
            with mounted_volume(volumes["Macintosh HD - Data"], target, authorize=True, owners=True):
                run(*sudo, "/bin/test", "!", "-e", target / "private/var/db/.AppleSetupDone")
                run(*sudo, *DITTO, source, target)
                run(*sudo, "/bin/test", "-f", target / "private/var/db/.AppleSetupDone")
        finally:
            run(HDI, "detach", source)
        for name in ("Preboot", "Recovery"):
            source = boot_assets / name / group
            with mounted_volume(volumes[name], target, authorize=True, owners=True):
                run(*sudo, "/bin/test", "!", "-e", target / group)
                for item in sorted(source.rglob("*")):
                    output = target / group / item.relative_to(source)
                    # Scratch may be ExFAT and cannot represent Unix ownership
                    # or modes. Apply them here rather than copying its metadata.
                    if item.is_dir():
                        run(*install, "-d", "-o", "0", "-g", "0", "-m", "0755", output)
                    else:
                        run(*install, "-d", "-o", "0", "-g", "0", "-m", "0755", output.parent)
                        run(*install, "-o", "0", "-g", "0", "-m", "0644", item, output)
