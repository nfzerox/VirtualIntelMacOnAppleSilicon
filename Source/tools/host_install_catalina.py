"""Prepare Catalina's System and Data group from original 19H15 packages."""

from contextlib import ExitStack
from functools import partial
import os
from pathlib import Path
import shutil
import stat
from uuid import uuid4

import host_install_apfs as apfs
from host_install_apfs_records import PLACEHOLDER, finalize_firmlinks
from host_install_hfs import (BUILDS, DITTO, HDI, PRELINKED_CACHE, SYSTEM_KERNEL, Target, attach,
                              catalog_permissions, finish_machine, mount_points, plist, restore_metadata,
                              run, stage_account, stage_prelinked_kernel)
from host_install_mojave import stage_pvg_graphics
from host_install_single_system_apfs import (CORE, KEXT_REPOSITORIES, PACKAGES, build_dyld_caches,
                                             collect_preboot, stage_auxiliary, stage_packages,
                                             stamp_prelinked_kernel)


TEMPLATE = Path("System/Library/Templates/Data")
SUDO = ("/usr/bin/sudo", "-n")


def parse_firmlinks(text):
    pairs = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        source, destination = line.split("\t")
        if source == "/AppleInternal" and destination == "AppleInternal":
            continue  # The native production installer omits this row.
        a, b = Path(source), Path(destination)
        if (not a.is_absolute() or b.is_absolute() or ".." in a.parts or ".." in b.parts or
                a == Path("/") or b == Path(".") or a.relative_to("/") in pairs):
            raise ValueError("Invalid Catalina firmlink manifest")
        pairs[a.relative_to("/")] = b
    if not {Path("private"), Path("Users"), Path("Applications"), Path("Library")} <= pairs.keys():
        raise ValueError("The Catalina firmlink manifest is missing its writable roots")
    if len(set(pairs.values())) != len(pairs) or any(
            p in q.parents for paths in (pairs.keys(), pairs.values()) for p in paths for q in paths):
        raise ValueError("The Catalina firmlink manifest has overlapping paths")
    return pairs


def materialize_data_template(target):
    """Make Apple's mutable defaults visible to account, PVG and cache tools."""
    root = target.root
    pairs = parse_firmlinks((root / "usr/share/firmlinks").read_text())
    metadata = list(target.metadata.items())
    installed_templates = {}
    for relative, (_, _, mode) in metadata:
        if relative.is_relative_to(TEMPLATE) and stat.S_ISDIR(mode) and mode & 0o500 != 0o500:
            # Catalina's stock dslocal Default template is 0600. Make the
            # unpublished copy traversable; HFS catalog finalization restores
            # that original mode on both copies before APFS publication.
            directory = root / relative
            directory.chmod(stat.S_IMODE(directory.stat().st_mode) | 0o500)
    for logical, data_relative in pairs.items():
        target.mkdir(logical)
        prefix = TEMPLATE / data_relative
        source = root / prefix
        if source.is_dir():
            run(*DITTO, source, root / logical)
            installed_templates[prefix] = logical
    for original, identity in metadata:
        if not original.is_relative_to(TEMPLATE):
            continue
        for prefix in (original, *original.parents):
            if prefix in installed_templates:
                logical = installed_templates[prefix]
                target.metadata[logical / original.relative_to(prefix)] = identity
                break
    # Reproduce the Core package's createApplicationsSymlinks action.
    utilities = Path("Applications/Utilities")
    target.mkdir(utilities)
    name = "System Information.app"
    link = utilities / name
    if (root / "System/Applications/Utilities" / name).is_dir() and not (root / link).exists():
        (root / link).symlink_to(f"../../System/Applications/Utilities/{name}")
        target.metadata[link] = (0, 0, stat.S_IFLNK | 0o755)
    return pairs


def project_volumes(source, system, data, pairs):
    """Copy the authenticated combined staging tree into two owned mounts."""
    ancestors = {ancestor for path in pairs for ancestor in path.parents}
    reserved = {}

    def directory(relative):
        target = system / relative
        original = (source / relative).stat()
        run(*SUDO, "/bin/mkdir", "-m", f"{stat.S_IMODE(original.st_mode):o}", target)
        run(*SUDO, "/usr/sbin/chown", f"{original.st_uid}:{original.st_gid}", target)
        return target

    def copy(relative):
        if relative in pairs:
            stub = directory(relative)
            destination = pairs[relative].as_posix().encode() + b"\0"
            run(*SUDO, "/usr/bin/xattr", "-wx", PLACEHOLDER[:-1].decode(), destination.hex(), stub)
            reserved[stub.stat().st_ino] = destination
            return
        original, dest = source / relative, system / relative
        if relative in ancestors:
            if relative != Path("."):
                directory(relative)
            for child in sorted(original.iterdir()):
                copy(relative / child.name)
        elif original.is_symlink():
            run(*SUDO, "/bin/cp", "-pP", original, dest)
        else:
            run(*SUDO, *DITTO, original, dest)

    # The template also includes Data-only folders such as home and mnt,
    # which are not exposed by a System-side firmlink.
    run(*SUDO, *DITTO, source / TEMPLATE, data)
    for logical, destination in pairs.items():
        run(*SUDO, "/bin/mkdir", "-p", (data / destination).parent)
        run(*SUDO, *DITTO, source / logical, data / destination)
    copy(Path("."))
    if len(reserved) != len(pairs):
        raise ValueError("One or more Catalina firmlink stubs were not copied")
    return reserved


def build_catalina(args, password, output, stage):
    guest = "catalina"
    runtime = args.app / "Contents/Resources/Runtime"
    base_image = args.base_system or args.media.with_name("BaseSystem.dmg")
    if not (base_image.parent / "BaseSystem.chunklist").is_file():
        raise ValueError("Catalina requires the original signed Recovery chunklist")
    identifier = str(uuid4()).upper()
    for name in ("Disks", "Firmware", "State", "Logs", "Installation", "Packages", "Preboot"):
        (stage / name).mkdir(mode=0o700)
    image, files = (stage / "Disks" / name for name in ("Staging.sparsebundle", "AppleFiles.sparsebundle"))
    run(HDI, "create", "-size", f"{args.size}g", "-type", "SPARSEBUNDLE", "-layout", "GPTSPUD", "-fs",
        "Journaled HFS+", "-volname", apfs.SPLIT_SEED, image)
    run(HDI, "create", "-size", "32g", "-type", "SPARSEBUNDLE", "-layout", "GPTSPUD", "-fs",
        "Journaled HFS+", "-volname", "Catalina Apple Files", files)
    with mount_points() as (media, root), mount_points() as (base, auxiliary), ExitStack() as sources:
        for source, mount in ((args.media, media), (base_image, base)):
            attach("-readonly", "-noverify", "-owners", "off", "-mountpoint", mount, source)
            sources.callback(run, HDI, "detach", mount)
        build = plist(base / CORE / "SystemVersion.plist")["ProductBuildVersion"]
        if build != BUILDS[guest]:
            raise ValueError("The Catalina recipe requires original 19H15 media")
        apfs.convert_partition(image, base)
        with apfs.attached_gpt(image) as (whole, _):
            volumes, group_uuid = apfs.create_split_volume_group(whole)
        system_uuid = volumes["Macintosh HD"]["APFSVolumeUUID"]
        data_uuid = volumes["Macintosh HD - Data"]["APFSVolumeUUID"]
        owner = {"FormatVersion": 1, "Kind": "VirtualIntelMacLegacySetup", "Guest": guest,
                 "MachineIdentifier": identifier, "VolumeUUID": data_uuid, "Enabled": True,
                 "AutomaticLogin": args.auto_login, "RemoteLogin": args.ssh, "FileSharing": False}
        attach("-owners", "off", "-mountpoint", root, files)
        try:
            disk = apfs.plist_command(apfs.DISKUTIL, "info", "-plist", root)
            target = Target(root, {})
            print("Extracting Catalina's original packages and Data template", flush=True)
            stage_packages(target, media, stage / "Packages", prepare_data=materialize_data_template)
            if plist(root / CORE / "SystemVersion.plist")["ProductBuildVersion"] != build:
                raise ValueError("Catalina InstallESD and BaseSystem builds differ")
            stage_pvg_graphics(target, runtime, stage, build, guest)
            stage_account(target, args.username, args.full_name or args.username, password, owner, args.auto_login, args.ssh)
            print("Building Catalina's startup and dyld caches on the host", flush=True)
            stage_prelinked_kernel(target, base, stage, guest, SYSTEM_KERNEL, (PRELINKED_CACHE,),
                                   repositories=KEXT_REPOSITORIES)
            stamp_prelinked_kernel(root)
            build_dyld_caches(target, base, stage / "Logs/Dyld.log", architectures=("x86_64",))
            collect_preboot(root, base, stage / "Preboot", system_uuid, catalina=True)
            pairs = parse_firmlinks((root / "usr/share/firmlinks").read_text())
            permissions = catalog_permissions(root, target.metadata)
        finally:
            run(HDI, "detach", root)
        restore_metadata(files, disk["PartitionMapPartitionOffset"], disk["Size"], permissions)
        # Keep extraction, patching and period cache generation unprivileged.
        # Only copy ownership-preserved files into the explicitly owned disks.
        print("Populating Catalina's System and Data volumes", flush=True)
        attach("-readonly", "-owners", "on", "-mountpoint", root, files)
        try:
            with apfs.attached_gpt(image) as (whole, _), mount_points() as (system, data):
                volumes, observed_group = apfs.verify_split_layout(whole + "s2")
                if observed_group != group_uuid:
                    raise ValueError("Catalina's staged volume group changed")
                with apfs.mounted_volume(volumes["Macintosh HD - Data"], data, authorize=True, owners=True), \
                        apfs.mounted_volume(volumes["Macintosh HD"], system, authorize=True, owners=True):
                    reserved = project_volumes(root, system, data, pairs)
                stage_auxiliary(volumes, auxiliary, base, base_image.parent, stage / "Preboot", system_uuid,
                                mount_volume=partial(apfs.mounted_volume, authorize=True),
                                authorize=True, catalina=True)
        finally:
            run(HDI, "detach", root)
        finalize_firmlinks(image, system_uuid, reserved)
        apfs.check_with_period_fsck(image, base)
    shutil.rmtree(files)
    for name in ("Packages", "Preboot"):
        shutil.rmtree(stage / name)
    finish_machine(args, runtime, output, stage, image, identifier, build, data_uuid, PACKAGES,
                   filesystem="APFS", detached=True)
