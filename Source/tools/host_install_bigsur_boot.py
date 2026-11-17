"""Stage the original Intel Big Sur boot assets for a new APFS group."""

from pathlib import Path
import plistlib
import re
import shutil
import struct
from uuid import UUID


BOOT = "AssetData/boot/"
FIRMWARE = BOOT + "Firmware/"
CORE = "System/Library/CoreServices/"
KC = "System/Library/KernelCollections/BootKernelExtensions.kc"
RECOVERY = "AssetData/Restore/"


def der_element(data, tag):
    if len(data) < 2 or data[0] != tag:
        raise ValueError("Unexpected Big Sur IMG4 structure")
    length, start = data[1], 2
    if length & 0x80:
        width = length & 0x7f
        if not 0 < width <= 4 or len(data) < start + width:
            raise ValueError("Invalid Big Sur IMG4 length")
        length = int.from_bytes(data[start:start + width], "big")
        start += width
    end = start + length
    if end > len(data):
        raise ValueError("Truncated Big Sur IMG4 element")
    return data[start:end], data[end:]


def intel_root_hash(image4_payload):
    """Read the expected SHA-256 seal from the signed asset's xsys payload."""
    sequence, trailing = der_element(image4_payload, 0x30)
    magic, sequence = der_element(sequence, 0x16)
    kind, sequence = der_element(sequence, 0x16)
    _, sequence = der_element(sequence, 0x16)  # Description.
    payload, rest = der_element(sequence, 0x04)
    if trailing or rest or magic != b"IM4P" or kind != b"xsys" or len(payload) < 48 or \
            struct.unpack_from("<QII", payload) != (2, 1, 32):
        raise ValueError("Unsupported Big Sur Intel root-hash format")
    return payload[16:48]


def copy_member(archive, member, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with archive.open(member) as source, destination.open("xb") as output:
        shutil.copyfileobj(source, output, 1024 * 1024)


def stage_intel_seal_assets(archive, root_member, preboot):
    """Publish the original seal inputs under the installed Intel boot paths."""
    prefix = FIRMWARE + "Manifests/Preboot/"
    for kind in ("root_hash", "mtree"):
        relative = f"usr/standalone/OS.dmg.{kind}"
        copy_member(archive, root_member.removesuffix("root_hash") + kind, preboot / relative)
        signatures = [name for name in archive.namelist()
                      if re.fullmatch(re.escape(prefix + relative) + r"\.[a-z0-9]+\.im4m", name)]
        if prefix + relative + ".x86legacyap.im4m" not in signatures:
            raise ValueError(f"Missing Intel Preboot seal manifest for {relative}")
        for member in signatures:
            copy_member(archive, member, preboot / member.removeprefix(prefix))


def stage_recovery_manifests(archive, recovery):
    """Flatten Apple's Intel RecoveryBoot manifests into the group directory."""
    prefix = FIRMWARE + "Manifests/RecoveryBoot/"
    artifacts = ("Firmware/BaseSystem.dmg.x86.trustcache", CORE + "boot.efi",
                 CORE + "bootbase.efi", KC)
    names = archive.namelist()
    for artifact in artifacts:
        pattern = re.compile(re.escape(prefix + artifact) + r"\.[a-z0-9]+\.im4m")
        matches = [member for member in names if pattern.fullmatch(member)]
        if prefix + artifact + ".x86legacyap.im4m" not in matches:
            raise ValueError(f"Missing Apple Intel Recovery boot manifest for {artifact}")
        for member in matches:
            copy_member(archive, member, recovery / Path(member).name)


def stage_boot_assets(archive, inventory, system_files, output, group_uuid):
    """Build unpublished Preboot/Recovery trees from this exact Apple build."""
    group = str(UUID(str(group_uuid))).upper()
    preboot, recovery = output / "Preboot" / group, output / "Recovery" / group
    preboot.mkdir(parents=True)
    recovery.mkdir(parents=True)
    for name in ("BridgeVersion.plist", "BridgeVersion.bin", "PlatformSupport.plist", "SystemVersion.plist"):
        copy_member(archive, BOOT + name, preboot / CORE / name)
    copy_member(archive, FIRMWARE + "usr/standalone/i386/boot.efi", preboot / CORE / "boot.efi")
    copy_member(archive, BOOT + "BuildManifest.plist", preboot / "restore/BuildManifest.plist")
    for name in (KC, KC + ".elides"):
        dest = preboot / "boot" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        # The installed kernel collection is from the original System
        # payload. Do not substitute a Recovery boot collection of the same size.
        shutil.copyfile(system_files / name, dest)
    for name in ("BaseSystem.dmg", "BaseSystem.chunklist"):
        copy_member(archive, RECOVERY + name, recovery / name)
    for name in ("PlatformSupport.plist", "SystemVersion.plist"):
        copy_member(archive, BOOT + name, recovery / name)
    stage_recovery_manifests(archive, recovery)

    root_member, = inventory["IntelRootHashes"]
    root_hash = intel_root_hash(archive.read(root_member))
    stage_intel_seal_assets(archive, root_member, preboot)
    stamp = Path(root_member).name
    pre_prefix = FIRMWARE + "Manifests/Preboot/"
    pattern = re.compile(r"\.[a-z0-9]+\.im4m$")
    installed = set()
    pre_boot_files = (CORE + "boot.efi", KC, "boot/" + KC,
                      "Firmware/" + stamp, "Firmware/" + stamp.replace("root_hash", "mtree"),
                      "Firmware/" + stamp.replace("root_hash", "trustcache"))
    for member in archive.namelist():
        if not pattern.search(member):
            continue
        if member.startswith(pre_prefix):
            rel = member.removeprefix(pre_prefix)
            if any(rel.startswith(name + ".") for name in pre_boot_files):
                copy_member(archive, member, preboot / rel)
                installed.add(rel)
    for required in pre_boot_files:
        if required + ".x86legacyap.im4m" not in installed:
            raise ValueError(f"Missing Apple Intel boot manifest for {required}")
    version = plistlib.loads((preboot / CORE / "SystemVersion.plist").read_bytes())
    if version["ProductBuildVersion"] != inventory["Build"]:
        raise ValueError("Big Sur boot files do not match the original System build")
    prefs = preboot / "Library/Preferences/SystemConfiguration"
    prefs.mkdir(parents=True)
    (prefs / "com.apple.Boot.plist").write_bytes(plistlib.dumps({"Kernel Flags": ""}))
    (preboot / CORE / ".disk_label.contentDetails").write_text("Macintosh HD")
    # .root_uuid is deliberately absent until the created System snapshot is
    # verified against the signed hash. Neither the System nor Data UUID fits.
    return root_hash
