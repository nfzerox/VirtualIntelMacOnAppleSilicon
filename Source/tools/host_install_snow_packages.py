"""Assemble the Snow Leopard 10J3250 clean install and PackageKit receipts."""

from datetime import datetime, timezone
from pathlib import Path
import plistlib
import stat
import subprocess
import tempfile

from host_install_leopard_packages import DENY_DELETE, apply_acl, unpack_flat_package
from host_install_tiger_packages import DITTO


# The original distribution's full OS, without the optional printer bundles
# (there are no printers attached to the new Mac). Keep the translations and
# X11, and explicitly select Apple's optional Rosetta to support PowerPC apps.
PACKAGES = (
    "BaseSystem", "Essentials", "BootCamp", "BSD", "iPodSupport",
    "PodcastCapture", "JavaTools", "AdditionalEssentials", "AdditionalSpeechVoices",
    "AsianLanguagesSupport", "MediaFiles", "MigrationAssistant", "Mail",
    "AddressBook", "iCal", "Automator", "DVDPlayer", "iTunes", "iChat",
    "Java", "Safari", "OxfordDictionaries", "AdditionalFonts", "Japanese",
    "German", "French", "Spanish", "Italian", "Dutch", "Danish", "Finnish",
    "Korean", "Norwegian", "Russian", "Swedish", "BrazilianPortuguese",
    "SimplifiedChinese", "TraditionalChinese", "Portuguese", "Polish",
    "X11User", "Rosetta",
)
RECEIPTS = Path("private/var/db/receipts")
VOLUMES_ACL = ("!#acl 1\ngroup:ABCDEFAB-CDEF-ABCD-EFAB-CDEF0000000C:"
               "everyone:12:deny,directory_inherit,only_inherit:write,append\n")


def receipt(name, info, timestamp, path_acls=None):
    result = {
        "InstallDate": datetime.fromtimestamp(timestamp, timezone.utc).replace(tzinfo=None),
        "InstallPrefixPath": "/",
        "InstallProcessName": "Mac OS X Installer",
        "PackageFileName": f"{name}.pkg",
        "PackageIdentifier": info.attrib["identifier"],
        "PackageVersion": info.attrib["version"],
    }
    groups = [g.text for g in info.findall("./groups/group")]
    if groups:
        result["PackageGroups"] = groups
    if path_acls:
        result["PathACLs"] = path_acls
    return result


def populate_packages(media, target, scratch, timestamp, packages=PACKAGES, *, base_system=None):
    media, target = Path(media).resolve(), Path(target).resolve()
    if not target.is_mount() or target == Path("/") or target.stat().st_dev == media.stat().st_dev:
        raise ValueError("Package assembly needs a separate, mounted scratch volume")
    source = media / ("Packages" if base_system else "System/Installation/Packages")
    metadata, native_receipts = {}, []
    with tempfile.TemporaryDirectory(prefix="flat-packages-", dir=scratch) as staging:
        for name in packages:
            info, bom, data = unpack_flat_package(source / f"{name}.pkg", Path(staging) / name,
                                                 target, bom_source=base_system if name == "BaseSystemResources" else None)
            metadata.update(data)
            native_receipts.append((name, info, bom))
        path_acls = {}
        if "Essentials" in packages:
            acls = apply_acl(target)
            base_package = "BaseSystemResources" if base_system else "BaseSystem"
            path_acls[base_package] = {p.as_posix(): DENY_DELETE for p in acls if p.parts[0] != "System"}
            path_acls["Essentials"] = {p.as_posix(): DENY_DELETE for p in acls if p.parts[0] == "System"}
            subprocess.run(("/bin/chmod", "+a", "everyone deny add_file,add_subdirectory,directory_inherit,only_inherit",
                            target / "Volumes"), check=True)
            path_acls[base_package]["Volumes"] = VOLUMES_ACL
        (target / RECEIPTS).mkdir(parents=True, exist_ok=True)
        metadata[RECEIPTS] = (0, 0, stat.S_IFDIR | 0o755)
        for name, info, bom in native_receipts:
            identifier = info.attrib["identifier"]
            bom_path = RECEIPTS / f"{identifier}.bom"
            plist_path = RECEIPTS / f"{identifier}.plist"
            subprocess.run((*DITTO, bom, target / bom_path), check=True)
            (target / plist_path).write_bytes(
                plistlib.dumps(receipt(name, info, timestamp, path_acls.get(name)), fmt=plistlib.FMT_BINARY))
            for path in (bom_path, plist_path):
                metadata[path] = (0, 0, stat.S_IFREG | 0o644)
    return metadata
