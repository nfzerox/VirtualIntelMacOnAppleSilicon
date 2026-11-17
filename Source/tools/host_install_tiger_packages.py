"""Apply Apple's Tiger core package data to a new, mounted HFS+ volume."""

from pathlib import Path, PurePosixPath
import stat
import subprocess


CORE = ("BaseSystem", "Essentials", "BSD")
# 8R4088's fresh OSInstall selections, confirmed by native receipts. The
# separate BundledSoftware and unchecked X11 choices are not part of this OS.
DEFAULT_PACKAGES = CORE + (
    "AdditionalEssentials", "AdditionalSpeechVoices", "AsianLanguagesSupport",
    "MediaFiles", "MigrationAssistant", "Mail", "AddressBook", "iCal",
    "Automator", "iTunes", "iChat", "Java", "Safari", "OxfordDictionaries",
    "BrotherPrinterDrivers", "CanonPrinterDrivers", "ElectronicsForImagingPrinterDrivers",
    "EpsonPrinterDrivers", "HewlettPackardPrinterDrivers", "LexmarkPrinterDrivers",
    "GimpPrintPrinterDrivers", "RicohPrinterDrivers", "XeroxPrinterDrivers",
    "AdditionalFonts", "Japanese", "German", "French", "Spanish", "Italian", "Dutch",
    "Danish", "Finnish", "Korean", "Norwegian", "Russian", "Swedish",
    "BrazilianPortuguese", "SimplifiedChinese", "TraditionalChinese",
)
DITTO = ("/usr/bin/ditto", "--rsrc", "--extattr", "--acl", "--noqtn",
         "--nohfsCompression", "--nopreserveHFSCompression")


def package_path(media, name):
    return media / "System/Installation/Packages" / f"{name}.pkg/Contents"


def package_metadata(media, packages=CORE):
    """Record intended ownership and modes from the selected Apple BOMs."""
    metadata = {}
    for name in packages:
        bom = package_path(media, name) / "Archive.bom"
        metadata.update(read_bom(bom))
    return metadata


def read_bom(bom):
    listing = subprocess.check_output(("/usr/bin/lsbom", "-p", "fmug", bom), text=True)
    metadata = {}
    for line in listing.splitlines():
        path, mode, uid, gid = line.split("\t")
        path = PurePosixPath(path)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"Unexpected path in {bom.name}")
        metadata[path] = int(uid), int(gid), int(mode, 8)
    return metadata


def populate_packages(media, target, packages=DEFAULT_PACKAGES, *, checkpoint=None):
    """Extract only package data. This intentionally never runs pkg scripts."""
    media, target = Path(media).resolve(), Path(target).resolve()
    if not target.is_mount() or target == Path("/") or target.stat().st_dev == media.stat().st_dev:
        raise ValueError("Tiger assembly needs a separate, mounted scratch volume")
    metadata = package_metadata(media, packages)
    if checkpoint:
        checkpoint()
    subprocess.run((*DITTO, "--bom", package_path(media, "BaseSystem") / "Archive.bom",
                    media, target), check=True)
    for name in packages[1:]:
        if checkpoint:
            checkpoint()
        archive = package_path(media, name) / "Archive.pax.gz"
        # The native pax extracts Apple's hardlink entries and consumes the
        # AppleDouble records into HFS resource forks and Finder attributes.
        subprocess.run(("/bin/pax", "-r", "-z", "-p", "p", "-f", archive), cwd=target, check=True)
    boot = PurePosixPath("System/Library/CoreServices/boot.efi")
    standalone = PurePosixPath("usr/standalone/i386/boot.efi")
    subprocess.run((*DITTO, target / standalone, target / boot), check=True)
    metadata[boot] = (0, 0, stat.S_IFREG | 0o644)
    return metadata


def catalog_permissions(target, metadata):
    """Resolve mounted paths to CNIDs, including the inodes of hardlinks."""
    permissions = {}
    # Some native Leopard packages contain real directories without search
    # permission. Traverse parents before children, opening only scratch
    # directories as needed. The detached catalog restores the exact modes.
    for relative in sorted(metadata, key=lambda p: len(p.parts)):
        expected = metadata[relative]
        path = target / relative
        try:
            item = path.lstat()
        except FileNotFoundError:
            if path.name.startswith("._"):
                # BOMs contain the original AppleDouble sidecars. pax uses
                # them to set native xattrs on the associated file. Empty
                # ACLs and resource forks leave no xattr to query afterward.
                data_path = path.with_name(path.name[2:])
                if data_path.relative_to(target) in metadata and (data_path.exists() or data_path.is_symlink()):
                    continue
            raise
        if stat.S_IFMT(item.st_mode) != stat.S_IFMT(expected[2]):
            raise ValueError(f"Package file type mismatch: {relative}")
        if stat.S_ISDIR(item.st_mode) and not item.st_mode & stat.S_IXUSR:
            path.chmod(stat.S_IMODE(item.st_mode) | stat.S_IXUSR)
        other = permissions.setdefault(item.st_ino, expected)
        if other != expected:
            raise ValueError(f"Conflicting package ownership or modes for {relative}")
    return permissions
