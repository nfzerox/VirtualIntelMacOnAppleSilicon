"""Leopard 9J3050 flat-package extraction and PackageKit 17 receipts."""

from pathlib import Path
import os
import re
import sqlite3
import stat
import subprocess
import sys
import tempfile
from xml.etree import ElementTree as ET

from host_install_tiger_packages import DITTO, read_bom


# Native 9J3050 clean Intel install order, from the distribution and verified
# against the native receipt database. No restore-only or migration scripts.
PACKAGES = (
    "BaseSystem", "Essentials", "BootCamp", "BSD", "iPodSupport", "PodcastCapture",
    "Directory", "JavaTools", "AdditionalEssentials", "AdditionalSpeechVoices",
    "AsianLanguagesSupport", "MediaFiles", "MigrationAssistant", "Mail", "AddressBook",
    "iCal", "Automator", "DVDPlayer", "iTunes", "iChat", "Java", "Safari",
    "OxfordDictionaries", "BrotherPrinterDrivers", "CanonPrinterDrivers",
    "EpsonPrinterDrivers", "HewlettPackardPrinterDrivers", "LexmarkPrinterDrivers",
    "GutenprintPrinterDrivers", "RicohPrinterDrivers", "XeroxPrinterDrivers",
    "FujiXeroxPrinterDrivers", "SamsungPrinterDrivers", "AdditionalFonts", "Japanese",
    "German", "French", "Spanish", "Italian", "Dutch", "Danish", "Finnish", "Korean",
    "Norwegian", "Russian", "Swedish", "BrazilianPortuguese", "SimplifiedChinese",
    "TraditionalChinese", "Portuguese", "Polish", "X11User", "ACL",
)

RECEIPTS = Path("Library/Receipts")
INSTALLER_UID = 96
ACL_PACKAGE = "com.apple.pkg.ACL"
DENY_DELETE = "!#acl 1\ngroup:ABCDEFAB-CDEF-ABCD-EFAB-CDEF0000000C:everyone:12:deny:delete\n"
PBZX_DECODER = Path(__file__).resolve().parents[1] / "scripts/decode-pbzx"

SCHEMA = """
PRAGMA user_version=17;
CREATE TABLE acls (path_key INTEGER NOT NULL, pkg_key INTEGER NOT NULL, acl VARCHAR NOT NULL);
CREATE TABLE groups (group_key INTEGER PRIMARY KEY AUTOINCREMENT, owner INTEGER NOT NULL, groupid VARCHAR NOT NULL);
CREATE TABLE oldpkgs (pkg_key INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, owner INTEGER NOT NULL, pkgid VARCHAR NOT NULL, vers VARCHAR NOT NULL, ppath VARCHAR NOT NULL, replaces INTEGER, replacedby INTEGER);
CREATE TABLE paths (path_key INTEGER PRIMARY KEY AUTOINCREMENT, path VARCHAR NOT NULL UNIQUE);
CREATE TABLE pkgs (pkg_key INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER NOT NULL, owner INTEGER NOT NULL, pkgid VARCHAR NOT NULL, vers VARCHAR NOT NULL, ppath VARCHAR NOT NULL, replaces INTEGER);
CREATE TABLE pkgs_groups (pkg_key INTEGER NOT NULL, group_key INTEGER NOT NULL);
CREATE TABLE pkgs_paths (pkg_key INTEGER NOT NULL, path_key INTEGER NOT NULL, uid INTEGER, gid INTEGER, perms INTEGER);
CREATE TABLE sha1s (path_key INTEGER NOT NULL, pkg_key INTEGER NOT NULL, sha1 BLOB NOT NULL);
CREATE TABLE taints (pkg_key INTEGER NOT NULL, taint VARCHAR NOT NULL);
CREATE INDEX paths_path_idx ON paths(path);
CREATE INDEX pkgs_pkgid_idx ON pkgs(pkgid);
CREATE INDEX pkgspaths_pathkey_idx ON pkgs_paths(path_key, pkg_key);
CREATE INDEX pkgspaths_pkgkey_idx ON pkgs_paths(pkg_key, path_key);
"""


class Receipts:
    def __init__(self, database):
        database.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(database)
        self.connection.executescript(SCHEMA)
        self.path_keys = {}
        self.group_keys = {}

    def add(self, identifier, version, groups, metadata, timestamp, *, acl_paths=()):
        db = self.connection
        package = db.execute("INSERT INTO pkgs (timestamp,owner,pkgid,vers,ppath) VALUES (?,0,?,?,'./')",
                             (timestamp, identifier, version)).lastrowid
        for group in groups:
            if group not in self.group_keys:
                self.group_keys[group] = db.execute(
                    "INSERT INTO groups (owner,groupid) VALUES (0,?)", (group,)).lastrowid
            db.execute("INSERT INTO pkgs_groups VALUES (?,?)", (package, self.group_keys[group]))

        additions = []
        rows = []
        for relative, (uid, gid, mode) in metadata.items():
            path = relative.as_posix()
            if path == ".":
                continue
            if path not in self.path_keys:
                key = len(self.path_keys) + 1
                self.path_keys[path] = key
                additions.append((key, path))
            rows.append((package, self.path_keys[path], uid, gid, mode))
        db.executemany("INSERT INTO paths (path_key,path) VALUES (?,?)", additions)
        db.executemany("INSERT INTO pkgs_paths VALUES (?,?,?,?,?)", rows)
        for path in acl_paths:
            relative = path.as_posix()
            if relative not in self.path_keys:
                self.path_keys[relative] = db.execute("INSERT INTO paths(path) VALUES (?)",
                                                      (relative,)).lastrowid
            db.execute("INSERT INTO acls VALUES (?,?,?)",
                       (self.path_keys[relative], package, DENY_DELETE))

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        try:
            if kind is None:
                self.connection.commit()
        finally:
            self.connection.close()


def apply_acl(target):
    """The fresh-install portion of 9J3050 ACL.pkg's directory policy."""
    paths = [Path("Applications"), Path("Library"), Path("Applications/Utilities")]
    for template in sorted((target / "System/Library/User Template").glob("*.lproj")):
        for folder, dirs, _ in os.walk(template, followlinks=False):
            parent = Path(folder)
            dirs[:] = sorted(d for d in dirs if not d.startswith(".") and
                             not (parent / d).is_symlink() and
                             (parent.name, d) not in (("Sites", "images"), ("Public", "Drop Box")))
            if parent != template:
                paths.append(parent.relative_to(target))
    subprocess.run(("/bin/chmod", "+a", "everyone deny delete",
                    *(target / relative for relative in paths)), check=True)
    return paths


def unpack_flat_package(package, folder, target, *, bom_source=None, stream_pbzx=False):
    """Unpack an Apple flat package without invoking its install scripts."""
    folder.mkdir()
    members = ("Bom", "PackageInfo") if bom_source or stream_pbzx else ("Bom", "PackageInfo", "Payload")
    subprocess.run(("/usr/bin/xar", "-x", "-C", folder, "-f", package, *members), check=True)
    info = ET.parse(folder / "PackageInfo").getroot()
    if not re.fullmatch(r"[A-Za-z0-9._-]+", info.attrib["identifier"]):
        raise ValueError(f"Invalid package identifier in {package.name}")
    if info.get("install-location", "/") != "/":
        raise ValueError(f"Unexpected install location in {package.name}")
    bom = folder / "Bom"
    data = read_bom(bom)
    if bom_source:
        # Lion's resource BOM is also an installation permissions list.
        # A few source directories are searchable in BaseSystem but the BOM
        # wants their stricter mode in the final image. Give ditto a staging
        # BOM so it does not remove search permission before copying children.
        entries = subprocess.check_output(("/usr/bin/lsbom", bom), text=True).splitlines()
        staging_entries = []
        for entry in entries:
            fields = entry.split("\t")
            relative, mode = fields[0], int(fields[1], 8)
            source = bom_source / relative
            if not source.exists() and not source.is_symlink():
                # Later ESDs retain receipt rows for resources not present
                # on their BaseSystem image (e.g. Mountain Lion's .licns).
                # Keep the original receipt BOM; no filesystem object was
                # supplied by this package, so do not assign it an owner.
                data.pop(Path(relative), None)
                continue
            if relative == "./.file" and mode == stat.S_IFREG and fields[3] == "0":
                # The original zero-byte HFS file marker is mode 000 and
                # cannot be opened on the read-only source by this process.
                (target / ".file").touch(exist_ok=False)
                continue
            if stat.S_ISDIR(mode) and not mode & stat.S_IXUSR:
                fields[1] = f"{mode | stat.S_IXUSR:o}"
                # A preceding flat package may have created the same
                # directory with the restrictive mode already applied.
                path = target / relative
                if path.is_dir() and not path.is_symlink():
                    path.chmod(stat.S_IMODE(path.stat().st_mode) | stat.S_IXUSR)
            staging_entries.append("\t".join(fields))
        listing, copy_bom = folder / "CopyListing", folder / "CopyBom"
        listing.write_text("\n".join(staging_entries) + "\n")
        subprocess.run(("/usr/bin/mkbom", "-i", listing, copy_bom), check=True)
        subprocess.run((*DITTO, "--bom", copy_bom, bom_source, target), check=True)
    elif stream_pbzx:
        # Stream the large Apple package directly from its read-only media.
        # Avoid a second multi-gigabyte copy on the scratch filesystem, and
        # require every stage to succeed, including XAR member verification.
        with subprocess.Popen(("/usr/bin/tar", "-xOf", package, "Payload"), stdout=subprocess.PIPE) as archive:
            try:
                with subprocess.Popen((sys.executable, PBZX_DECODER, "-"), stdin=archive.stdout,
                                      stdout=subprocess.PIPE) as decoder:
                    archive.stdout.close()
                    try:
                        result = subprocess.run(("/bin/pax", "-r", "-p", "p"), stdin=decoder.stdout, cwd=target)
                    finally:
                        decoder.stdout.close()
            finally:
                archive.stdout.close()
        if archive.returncode or decoder.returncode or result.returncode:
            raise ValueError(f"Could not stream and verify {package.name} (archive {archive.returncode}, "
                             f"decoder {decoder.returncode}, extraction {result.returncode})")
    else:
        payload = folder / "Payload"
        with payload.open("rb") as archive:
            signature = archive.read(6)
        decoder = None
        if signature.startswith(b"BZh"):
            decoder = ("/usr/bin/bzip2", "-dc")
        elif signature.startswith(b"pbzx"):
            decoder = (sys.executable, PBZX_DECODER)
        if decoder:
            with subprocess.Popen((*decoder, payload), stdout=subprocess.PIPE) as decompressor:
                try:
                    subprocess.run(("/bin/pax", "-r", "-p", "p"), stdin=decompressor.stdout,
                                   cwd=target, check=True)
                finally:
                    decompressor.stdout.close()
                if decompressor.wait():
                    raise ValueError(f"Could not decompress {package.name}")
        else:
            gzip = signature.startswith(b"\x1f\x8b")
            if not gzip and signature not in (b"070707", b"070701", b"070702"):
                raise ValueError(f"Unrecognized payload format in {package.name}")
            subprocess.run(("/bin/pax", "-r", *(("-z",) if gzip else ()), "-p", "p", "-f", payload),
                           cwd=target, check=True)
        payload.unlink()
    return info, bom, data


def populate_packages(media, target, scratch, timestamp, packages=PACKAGES):
    media, target = Path(media).resolve(), Path(target).resolve()
    if not target.is_mount() or target == Path("/") or target.stat().st_dev == media.stat().st_dev:
        raise ValueError("Leopard assembly needs a separate, mounted scratch volume")
    package_root = media / "System/Installation/Packages"
    database = RECEIPTS / "db/a.receiptdb"
    boms = RECEIPTS / "boms"
    # Native payloads may also describe receipt ancestors, so create the
    # database only after extracting all of them. Keep one extracted payload
    # at a time on scratch, never on the host's system volume.
    metadata, receipts = {}, []
    with tempfile.TemporaryDirectory(prefix="flat-packages-", dir=scratch) as staging:
        staging = Path(staging)
        for name in packages:
            package = package_root / f"{name}.pkg"
            folder = staging / name
            info, bom, data = unpack_flat_package(package, folder, target)
            identifier, version = info.attrib["identifier"], info.attrib["version"]
            metadata.update(data)
            receipts.append((identifier, version, [g.text for g in info.findall("./groups/group")],
                             bom, data))
        (target / boms).mkdir(parents=True, exist_ok=True)
        acl_paths = apply_acl(target) if "ACL" in packages else []
        with Receipts(target / database) as db:
            for identifier, version, groups, bom, data in receipts:
                relative = boms / f"{identifier}.bom"
                subprocess.run((*DITTO, bom, target / relative), check=True)
                metadata[relative] = (INSTALLER_UID, 0, stat.S_IFREG | 0o644)
                db.add(identifier, version, groups, data, timestamp,
                       acl_paths=acl_paths if identifier == ACL_PACKAGE else ())
    metadata[boms] = (INSTALLER_UID, 0, stat.S_IFDIR | 0o755)
    metadata[database.parent] = (INSTALLER_UID, 80, stat.S_IFDIR | 0o755)
    metadata[database] = (INSTALLER_UID, 0, stat.S_IFREG | 0o600)
    return metadata
