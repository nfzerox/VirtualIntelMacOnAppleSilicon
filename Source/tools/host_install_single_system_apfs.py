"""Assemble a single-system APFS guest from its Apple InstallESD and BaseSystem."""

from contextlib import ExitStack
from functools import partial
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import time
from uuid import UUID, uuid4

import host_install_apfs as apfs
from host_install_hfs import (APFS_CHECKPOINT, BUILDS, HDI, PRELINKED_CACHE, STARTUP_CACHE, SYSTEM_KERNEL, TITLES, Target, attach,
                              catalog_permissions, finish_machine, mount_points, plist, restore_metadata,
                              run, stage_account, stage_prelinked_kernel, stage_support)
from host_install_leopard_packages import DENY_DELETE, apply_acl, unpack_flat_package
from host_install_snow_packages import VOLUMES_ACL, receipt


CORE = Path("System/Library/CoreServices")
BOOT_PLIST = Path("Library/Preferences/SystemConfiguration/com.apple.Boot.plist")
PRELINK = Path(PRELINKED_CACHE).parent
PACKAGES = ("Core", "EmbeddedOSFirmware", "SecureBoot")
PREBOOT_FILES = ("boot.efi", "boot.efi.j137ap.im4m", "bootbase.efi.j137ap.im4m",
                 "BridgeVersion.bin", "PlatformSupport.plist", "SystemVersion.plist")
KEXT_REPOSITORIES = ("System/Library/Extensions", "Library/Extensions")


def stage_packages(target, media, stage, *, prepare_data=None):
    records = []
    for name in PACKAGES:
        package = media / "Packages" / f"{name}.pkg"
        info, bom, metadata = unpack_flat_package(package, stage / name, target.root, stream_pbzx=name == "Core")
        target.metadata.update(metadata)
        records.append((name, info, bom))
    if prepare_data is not None:
        prepare_data(target)
    acls = apply_acl(target.root)
    acls.append(Path("System"))
    # Apple's Core postinstall applies deny-delete ACLs to its
    # standard app bundles in addition to the parent and template ACLs.
    applications = target.root / "Applications"
    apps = sorted((*applications.glob("*.app"), *(applications / "Utilities").glob("*.app")))
    acls.extend(p.relative_to(target.root) for p in apps if not p.is_symlink())
    run("/bin/chmod", "+a", "everyone deny delete", target.root / "System", *apps)
    run("/bin/chmod", "+a", "everyone deny add_file,add_subdirectory,directory_inherit,only_inherit",
        target.root / "Volumes")
    path_acls = {p.as_posix(): DENY_DELETE for p in acls}
    path_acls["Volumes"] = VOLUMES_ACL
    timestamp = int(time.time())
    for name, info, bom in records:
        folder = Path("System/Library/Receipts")
        identifier = info.attrib["identifier"]
        if identifier != f"com.apple.pkg.{name}":
            raise ValueError(f"Unexpected original package identifier: {identifier}")
        target.copy_file(bom, folder / f"{identifier}.bom")
        data = receipt(name, info, timestamp, path_acls if name == "Core" else None)
        data["InstallProcessName"] = "macOS Installer"
        target.write(folder / f"{identifier}.plist", plistlib.dumps(data, fmt=plistlib.FMT_BINARY))
    hidden = ("automount", "bin", "cores", "dev", "etc", "private", "sbin", "tmp", "usr", "var", "Volumes",
              "opt", "Network")
    run("/usr/bin/chflags", "-h", "hidden", *(target.root / p for p in hidden if (target.root / p).exists()))
    target.write("usr/local/.com.apple.installer.keep", b"")
    target.copy_file(target.root / "usr/standalone/i386/boot.efi", CORE / "boot.efi")
    if not (target.root / PRELINKED_CACHE).stat().st_size:
        raise ValueError("The original Core package lacks its prelinked kernel")
    # Catalina's native Data-side Startup folder does not contain this
    # High Sierra / Mojave compatibility alias.
    if prepare_data is None:
        cache = target.root / STARTUP_CACHE
        target.mkdir(Path(STARTUP_CACHE).parent)
        if cache.is_symlink() or cache.exists():
            cache.unlink()
        cache.symlink_to("../../../PrelinkedKernels/prelinkedkernel")
        target.metadata[Path(STARTUP_CACHE)] = (0, 0, stat.S_IFLNK | 0o755)


def collect_preboot(system, base, staged, system_uuid, *, catalina=False):
    target = Target(staged, {})
    for name in PREBOOT_FILES:
        source = base if name == "bootbase.efi.j137ap.im4m" else system
        target.copy_file(source / CORE / name, CORE / name)
    if catalina:
        for source, pattern in ((system, "boot.efi.*.im4m"), (base, "bootbase.efi.*.im4m")):
            for file in sorted((source / CORE).glob(pattern)):
                if file.name not in PREBOOT_FILES:
                    target.copy_file(file, CORE / file.name)
        target.copy_file(system / CORE / "BridgeVersion.plist", CORE / "BridgeVersion.plist")
    target.copy_file(system / BOOT_PLIST, BOOT_PLIST)
    target.write(CORE / ".root_uuid", system_uuid.encode("ascii"))
    target.write(CORE / ".contentDetails", b"Macintosh HD")
    efi = Path("usr/standalone/i386/EfiLoginUI")
    for source in sorted((system / efi).glob("*.efires")):
        target.copy_file(source, efi / source.name)
        if source.name not in ("recoveryUI.efires", "recovery_user.efires"):
            cache = Path("System/Library/Caches/com.apple.corestorage/EFILoginLocalizations")
            target.copy_file(source, cache / source.name)


def stamp_prelinked_kernel(root):
    # Match High Sierra's check_kext_boot_cache_file (kext_tools-460.70.2).
    # It checks equality, not simply that the cache is newer. Apply the
    # timestamp to the newly linked cache, after the final kext files are set.
    def seconds(path):
        return (root / path).stat().st_mtime_ns // 1_000_000_000

    expected = 1 + max(map(seconds, KEXT_REPOSITORIES))
    kernel = seconds(SYSTEM_KERNEL)
    if kernel > expected:
        expected = kernel + 1
    cache = root / PRELINKED_CACHE
    os.utime(cache, ns=(cache.stat().st_atime_ns, expected * 1_000_000_000))


def build_dyld_caches(target, base, log, *, architectures=("x86_64", "i386")):
    # High Sierra's native installer builds both architectures on the target.
    # Use that release's generator on the installed loose files, never the
    # host's libraries or a cache from an already-installed reference Mac.
    # Use the installed Apple receipts to discover Mach-O files. The
    # generator's alternate directory scan produced malformed runtime paths
    # such as /usr//lib and a cache that broke authorizationhost's PAM load.
    relative = Path("private/var/db/dyld")
    target.mkdir(relative)
    folder = target.root / relative
    builder = base / "usr/bin/update_dyld_shared_cache"
    with log.open("wb") as stream:
        selections = [argument for arch in architectures for argument in ("-arch", arch)]
        result = subprocess.run(("/usr/bin/arch", "-x86_64", builder, "-root", target.root,
                                 "-cache_dir", folder, *selections, "-force"), stdin=subprocess.DEVNULL,
                                stdout=stream, stderr=subprocess.STDOUT)
    # The period utility can exit zero after reporting a build failure.
    for arch in architectures:
        cache = folder / f"dyld_shared_cache_{arch}"
        if result.returncode or not cache.is_file():
            raise ValueError(f"The original Apple tool could not build the {arch} shared cache; see {log}")
        with cache.open("rb") as stream:
            if stream.read(16).rstrip(b"\0").split() != [b"dyld_v1", arch.encode("ascii")]:
                raise ValueError(f"Unexpected {arch} shared-cache format; see {log}")
    target.remember_tree(relative, 0, 0)


def stage_auxiliary(volumes, root, base, recovery_media, preboot, system_uuid, *,
                    mount_volume=apfs.mounted_volume, authorize=False, catalina=False):
    with mount_volume(volumes["Preboot"], root):
        tree = root / system_uuid
        run("/usr/bin/ditto", preboot, tree)
        # No --setBoot: the app boots its own firmware, not the Mac's.
        prefix = ("/usr/bin/sudo", "-n") if authorize else ()
        run(*prefix, "/usr/sbin/bless", "--folder", tree / CORE, "--file", tree / CORE / "boot.efi")
    with mount_volume(volumes["Recovery"], root):
        tree = root / system_uuid
        tree.mkdir(exist_ok=True)
        for name in ("boot.efi", "boot.efi.j137ap.im4m", "PlatformSupport.plist", "SystemVersion.plist"):
            shutil.copyfile(base / CORE / name, tree / name)
        for name in ("immutablekernel", "immutablekernel.j137ap.im4m"):
            shutil.copyfile(base / PRELINK / name, tree / name)
        if catalina:
            for source, pattern in ((base / CORE, "boot.efi.*.im4m"), (base / PRELINK, "immutablekernel.*.im4m")):
                for file in sorted(source.glob(pattern)):
                    shutil.copyfile(file, tree / file.name)
        if (base / PRELINK / "immutablekernel").samefile(base / PRELINK / "prelinkedkernel"):
            cache = tree / "prelinkedkernel"
            if not cache.exists() or not cache.samefile(tree / "immutablekernel"):
                cache.unlink(missing_ok=True)
                cache.hardlink_to(tree / "immutablekernel")
        else:
            shutil.copyfile(base / PRELINK / "prelinkedkernel", tree / "prelinkedkernel")
        for name in ("BaseSystem.dmg", "BaseSystem.chunklist"):
            shutil.copyfile(recovery_media / name, tree / name)
        (tree / "com.apple.Boot.plist").write_bytes(plistlib.dumps({
            "Kernel Cache": f"\\{system_uuid}\\immutablekernel",
            "Kernel Flags": f"root-dmg=file:///{system_uuid}/BaseSystem.dmg",
        }))


def finalize_apfs_package(args, output, stage, image, identifier, build, system_uuid):
    app = args.app / "Contents/Resources/Runtime"
    finish_machine(args, app, output, stage, image, identifier, build, system_uuid, PACKAGES, filesystem="APFS")
    (output / APFS_CHECKPOINT).unlink(missing_ok=True)
    for name in ("Packages", "Preboot"):
        shutil.rmtree(output / name)


def resume_apfs_assembly(args):
    output, stage = args.output.resolve(), args.resume_apfs_stage.resolve()
    if (output.exists() or output.suffix != ".vintelmac" or stage.parent != output.parent or
            not stage.name.startswith(f".{args.guest}-host-")):
        raise ValueError("Choose the retained scratch stage and a new output beside it")
    record = plist(stage / APFS_CHECKPOINT)
    build = BUILDS[args.guest]
    if (record.get("FormatVersion") != 1 or record.get("Guest") != args.guest or
            record.get("ProductBuildVersion") != build or record.get("DiskGiB") != args.size):
        raise ValueError("The scratch checkpoint does not match the requested guest and disk size")
    identifier = str(UUID(record["MachineIdentifier"])).upper()
    system_uuid = str(UUID(record["VolumeUUID"])).upper()
    args.ssh, args.ssh_port = record["SSHEnabled"], record["SSHPort"]
    image = stage / "Disks/Staging.sparsebundle"
    base_image = args.base_system or args.media.with_name("BaseSystem.dmg")
    with mount_points() as (base, root), ExitStack() as sources:
        attach("-readonly", "-noverify", "-owners", "off", "-mountpoint", base, base_image)
        sources.callback(run, HDI, "detach", base)
        if plist(base / CORE / "SystemVersion.plist")["ProductBuildVersion"] != build:
            raise ValueError("The recovery source does not match the staged build")
        with apfs.attached_gpt(image) as (whole, _):
            volumes = apfs.verify_layout(whole + "s2")
            if volumes["Macintosh HD"]["APFSVolumeUUID"] != system_uuid:
                raise ValueError("The scratch checkpoint does not match this APFS system")
            mount_volume = partial(apfs.mounted_volume, authorize=args.authorize_apfs_mounts)
            stage_auxiliary(volumes, root, base, base_image.parent, stage / "Preboot", system_uuid,
                            mount_volume=mount_volume, authorize=args.authorize_apfs_mounts)
        apfs.check_with_period_fsck(image, base)
    finalize_apfs_package(args, output, stage, image, identifier, build, system_uuid)


def build_single_system_apfs(args, password, output, stage):
    guest = args.guest
    title = TITLES[guest]
    app = args.app / "Contents/Resources/Runtime"
    base_image = args.base_system or args.media.with_name("BaseSystem.dmg")
    if not base_image.is_file() or not (base_image.parent / "BaseSystem.chunklist").is_file():
        raise ValueError(f"{title} requires the original BaseSystem.dmg and BaseSystem.chunklist")
    identifier = str(uuid4()).upper()
    for name in ("Disks", "Firmware", "State", "Logs", "Installation", "Packages", "Preboot"):
        (stage / name).mkdir(mode=0o700)
    image = stage / "Disks/Staging.sparsebundle"
    print(f"Creating {title}’s native staging disk", flush=True)
    run(HDI, "create", "-size", f"{args.size}g", "-type", "SPARSEBUNDLE", "-layout", "GPTSPUD", "-fs",
        "Journaled HFS+", "-volname", "Macintosh HD", image)
    with mount_points() as (media, root), mount_points() as (base, _), ExitStack() as sources:
        for source, destination in ((args.media, media), (base_image, base)):
            attach("-readonly", "-noverify", "-owners", "off", "-mountpoint", destination, source)
            sources.callback(run, HDI, "detach", destination)
        build = plist(base / CORE / "SystemVersion.plist")["ProductBuildVersion"]
        if build != BUILDS[guest]:
            raise ValueError(f"The {title} recipe requires original {BUILDS[guest]} media")
        attach("-owners", "off", "-mountpoint", root, image)
        try:
            disk = apfs.plist_command(apfs.DISKUTIL, "info", "-plist", root)
            system_uuid = disk["VolumeUUID"]
            if not re.fullmatch(r"[0-9A-F-]{36}", system_uuid):
                raise ValueError("Unexpected staging volume identity")
            owner = {"FormatVersion": 1, "Kind": "VirtualIntelMacLegacySetup", "Guest": guest,
                     "MachineIdentifier": identifier, "VolumeUUID": system_uuid, "Enabled": True,
                     "AutomaticLogin": args.auto_login, "RemoteLogin": args.ssh, "FileSharing": False}
            target = Target(root, {})
            print(f"Extracting Apple’s {title} packages", flush=True)
            stage_packages(target, media, stage / "Packages")
            if plist(root / CORE / "SystemVersion.plist")["ProductBuildVersion"] != build:
                raise ValueError(f"{title} InstallESD and BaseSystem do not contain the same build")
            print("Preparing the account and graphics", flush=True)
            if guest == "mojave":
                from host_install_mojave import stage_pvg_graphics
                stage_pvg_graphics(target, app, stage, build, guest)
            else:
                stage_support(target, app / f"OpenGLGuests/{guest}", owner, build)
            stage_account(target, args.username, args.full_name or args.username, password,
                          owner, args.auto_login, args.ssh)
            print(f"Prelinking {title}’s startup kernel on the host", flush=True)
            stage_prelinked_kernel(target, base, stage, guest, SYSTEM_KERNEL, (PRELINKED_CACHE,),
                                   repositories=KEXT_REPOSITORIES)
            stamp_prelinked_kernel(root)
            collect_preboot(root, base, stage / "Preboot", system_uuid)
            print(f"Building {title}’s native dyld caches on the host", flush=True)
            build_dyld_caches(target, base, stage / "Logs/Dyld.log")
            permissions = catalog_permissions(root, target.metadata)
            boot = (root / CORE).stat().st_ino, (root / CORE / "boot.efi").stat().st_ino
        finally:
            run(HDI, "detach", root)
        restore_metadata(image, disk["PartitionMapPartitionOffset"], disk["Size"], permissions, boot=boot)
        print(f"Converting to {title}’s native APFS and preparing Recovery", flush=True)
        apfs.convert_partition(image, base)
        with apfs.attached_gpt(image) as (whole, _):
            volumes = apfs.add_volumes(whole, system_uuid)
            with (stage / APFS_CHECKPOINT).open("xb") as checkpoint:
                plistlib.dump({"FormatVersion": 1, "Guest": guest, "ProductBuildVersion": build,
                               "MachineIdentifier": identifier, "VolumeUUID": system_uuid, "DiskGiB": args.size,
                               "SSHEnabled": args.ssh, "SSHPort": args.ssh_port}, checkpoint)
                checkpoint.flush()
                os.fsync(checkpoint.fileno())
            mount_volume=partial(apfs.mounted_volume, authorize=args.authorize_apfs_mounts)
            stage_auxiliary(volumes, root, base, base_image.parent, stage / "Preboot", system_uuid,
                            mount_volume=mount_volume, authorize=args.authorize_apfs_mounts)
        apfs.check_with_period_fsck(image, base)
    finalize_apfs_package(args, output, stage, image, identifier, build, system_uuid)
