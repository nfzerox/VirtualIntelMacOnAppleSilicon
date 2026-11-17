"""Build a High Sierra-compatible APFS layout on a newly owned disk image.

The period converter creates the container and embeds its EFI driver. The
host kernel creates and mounts the auxiliary volumes; the period checker
verifies their on-disk structures.
Nothing in this module installs software, blesses the host or sets its NVRAM.
"""

from contextlib import contextmanager
from pathlib import Path
import plistlib
import re
import shutil
import stat
import struct
import subprocess
import tempfile
from uuid import UUID
import zlib

from host_install_hfs_metadata import RawDisk, SparseBundle
from host_install_hfs_recovery import HFS, SECTOR, gpt_header


HDI = "/usr/bin/hdiutil"
DISKUTIL = "/usr/sbin/diskutil"
APFS = UUID("7c3457ef-0000-11aa-aa11-00306543ecac")
EFI = UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
RESOURCES = Path("System/Library/Filesystems/apfs.fs/Contents/Resources")
DEVICE = re.compile(r"disk[0-9]+(?:s[0-9]+)?")
ROLES = (("Macintosh HD", "0", []), ("Preboot", "B", ["Preboot"]),
         ("Recovery", "R", ["Recovery"]), ("VM", "V", ["VM"]))
SPLIT_SEED = "Virtual Intel Mac APFS Seed"
SPLIT_ROLES = {**{name: roles for name, _, roles in ROLES[1:]},
               "Macintosh HD": ["System"], "Macintosh HD - Data": ["Data"]}
UPDATE_VOLUME = ("Update", "E", ["Update"])


def plist_command(*command):
    return plistlib.loads(subprocess.check_output(command))


def run(*command):
    subprocess.run(command, stdin=subprocess.DEVNULL, check=True)


def device(name):
    if not DEVICE.fullmatch(name):
        raise ValueError("Unexpected disk identifier for the new image")
    return f"/dev/{name}"


def attached_images(image):
    image = image.resolve()
    return [item for item in plist_command(HDI, "info", "-plist")["images"]
            if Path(item["image-path"]).resolve() == image]


def image_entities(image):
    matches = [item["system-entities"] for item in attached_images(image)]
    if len(matches) != 1:
        raise ValueError("Cannot identify the attached scratch disk")
    return matches[0]


@contextmanager
def attached_gpt(image, *, readonly=False):
    result = plist_command(HDI, "attach", "-plist", "-nomount", "-nobrowse", "-noautoopen",
                           *(('-readonly',) if readonly else ()), image)
    whole, = (entry["dev-entry"].removeprefix("/dev/") for entry in result["system-entities"]
              if entry.get("content-hint") == "GUID_partition_scheme")
    try:
        yield whole, result["system-entities"]
    finally:
        run(HDI, "detach", device(whole))


def read_efi_jumpstart(image, start, end):
    """Read the original EFI driver from one APFS GPT slice."""
    offset, capacity = start * SECTOR, (end + 1 - start) // 8
    superblock = image.read(offset, 4096)
    count, = struct.unpack_from("<Q", superblock, 40)
    if superblock[32:40] != b"NXSB\x00\x10\x00\x00" or not 0 < count <= capacity:
        raise ValueError("The partition does not contain a 4 KiB APFS container")
    # Apple File System Reference, EFI Jumpstart and nx_superblock_t.
    jumpstart, = struct.unpack_from("<Q", superblock, 0x4f8)
    if not 0 < jumpstart < count:
        raise ValueError("The container has no embedded APFS EFI driver")
    record = image.read(offset + jumpstart * 4096, 4096)
    magic, version, size, extents = struct.unpack_from("<4sIII", record, 32)
    if magic != b"JSDR" or version != 1 or not 0 < size <= 16 * 2**20 or not 0 < extents <= (4096 - 176) // 16:
        raise ValueError("Unexpected period APFS EFI driver record")
    copied = bytearray()
    for first, blocks in struct.iter_unpack("<QQ", record[176:176 + 16 * extents]):
        if not 0 < first < first + blocks <= count or len(copied) + blocks * 4096 >= size + 4096:
            raise ValueError("Invalid embedded EFI driver extent")
        copied.extend(image.read(offset + first * 4096, blocks * 4096))
    if len(copied) < size or not copied.startswith(b"MZ"):
        raise ValueError("Incomplete APFS EFI driver")
    return bytes(copied[:size])


def recovery_efi_driver(image):
    """Extract EFI Jumpstart from the original detached Big Sur Recovery image."""
    primary = gpt_header(image, 1)
    backup = gpt_header(image, primary[2])
    entries = primary[6]
    if backup[2] != 1 or primary[3:5] != backup[3:5] or entries != backup[6]:
        raise ValueError("The recovery disk's GPT copies disagree")
    extent, = (struct.unpack_from("<QQ", entries, index + 32) for index in range(0, len(entries), 128)
               if entries[index:index + 16] == APFS.bytes_le)
    if not primary[3] <= extent[0] <= extent[1] <= primary[4]:
        raise ValueError("The recovery APFS partition is outside its GPT bounds")
    return read_efi_jumpstart(image, *extent)


def publish_apfs_partition(image_path, driver):
    """Change only the type of the newly formatted, detached GPT slice."""
    image = SparseBundle(image_path)
    primary = gpt_header(image, 1)
    backup = gpt_header(image, primary[2])
    entries = primary[6]
    if (backup[2] != 1 or primary[3:5] != backup[3:5] or entries != backup[6] or
            entries[:16] != EFI.bytes_le or entries[128:144] != HFS.bytes_le or any(entries[256:])):
        raise ValueError("Expected a fresh GPT with EFI and exactly one staging partition")
    start, end = struct.unpack_from("<QQ", entries, 160)
    if start % 8 or (end + 1) % 8:
        raise ValueError("The new APFS partition is not block aligned")
    if read_efi_jumpstart(image, start, end) != driver.read_bytes():
        raise ValueError("The embedded EFI driver does not match original media")
    entries[128:144] = APFS.bytes_le
    for header, length, _, _, _, table, _ in (backup, primary):
        struct.pack_into("<I", header, 88, zlib.crc32(entries))
        struct.pack_into("<I", header, 16, zlib.crc32(header[:length]))
        image.write(table * SECTOR, entries)
        image.write(struct.unpack_from("<Q", header, 24)[0] * SECTOR, header)


def convert_partition(image, base, *, driver=None):
    driver = driver or base / "usr/standalone/i386/apfs.efi"
    with attached_gpt(image) as (whole, entities):
        entry, = (item for item in entities if item.get("content-hint") == "Apple_HFS")
        if entry["dev-entry"] != device(whole + "s2"):
            raise ValueError("Unexpected disk geometry for the new APFS image")
        # This old executable prefixes the short partition identifier with
        # /dev/r itself. Giving it a full /dev path would fail before opening.
        run("/usr/bin/arch", "-x86_64", base / RESOURCES / "hfs_convert", f"--efi={driver}", whole + "s2")
    publish_apfs_partition(image, driver)


def container_info(store):
    return next(item for item in plist_command(DISKUTIL, "apfs", "list", "-plist")["Containers"]
                if [entry["DeviceIdentifier"] for entry in item["PhysicalStores"]] == [store])


def add_volumes(whole, system_uuid):
    store = whole + "s2"
    container = container_info(store)
    volumes = container["Volumes"]
    if (len(volumes) != 1 or volumes[0]["Name"] != "Macintosh HD" or volumes[0]["Roles"] or
            volumes[0]["APFSVolumeUUID"] != system_uuid):
        raise ValueError("Conversion did not preserve the identity of Macintosh HD")
    reference = device(container["ContainerReference"])
    for name, role, _ in ROLES[1:]:
        run(DISKUTIL, "apfs", "addVolume", reference, "APFS", name, "-role", role, "-nomount")
    return verify_layout(store)


def verify_layout(store):
    volumes = container_info(store)["Volumes"]
    if [(v["Name"], v["Roles"]) for v in volumes] != [(name, roles) for name, _, roles in ROLES]:
        raise ValueError("The new APFS container does not have the expected High Sierra volumes")
    return {entry["Name"]: entry for entry in volumes}


def verify_split_layout(store, *, include_update=False):
    container = container_info(store)
    volumes = container["Volumes"]
    roles = {**SPLIT_ROLES, UPDATE_VOLUME[0]: UPDATE_VOLUME[2]} if include_update else SPLIT_ROLES
    if len(volumes) != len(roles) or {v["Name"]: v["Roles"] for v in volumes} != roles:
        raise ValueError("The split APFS disk lacks its native System, Data and auxiliary volumes")
    named = {v["Name"]: v for v in volumes}
    groups, = plist_command(DISKUTIL, "apfs", "listVolumeGroups", "-plist",
                            container["ContainerReference"])["Containers"]
    if groups["ContainerReference"] != container["ContainerReference"] or len(groups["VolumeGroups"]) != 1:
        raise ValueError("Expected one APFS System/Data volume group")
    group, = groups["VolumeGroups"]
    members = {(v["Role"], v["DiskUUID"]) for v in group["Volumes"]}
    expected = {(role, named[name]["APFSVolumeUUID"])
                for name, role in (("Macintosh HD", "System"), ("Macintosh HD - Data", "Data"))}
    if len(group["Volumes"]) != 2 or members != expected:
        raise ValueError("The APFS System and Data volumes are not paired")
    return named, str(UUID(group["APFSVolumeGroupUUID"])).upper()


def create_split_volume_group(whole, *, include_update=False):
    """Replace the empty period-converted seed on our unpublished GPT."""
    store = whole + "s2"
    container = container_info(store)
    seed, = container["Volumes"]
    if seed["Name"] != SPLIT_SEED or seed["Roles"] or seed.get("MountPoint"):
        raise ValueError("Expected the unmounted, empty APFS seed volume")
    reference = container["ContainerReference"]
    run(DISKUTIL, "apfs", "addVolume", reference, "APFS", "Macintosh HD - Data", "-role", "D", "-nomount")
    data, = (v for v in container_info(store)["Volumes"] if v["Roles"] == ["Data"])
    # A System-first pair was refused by the host. Creating System against
    # the Data anchor yields the native volume group in both metadata views.
    run(DISKUTIL, "apfs", "addVolume", reference, "APFS", "Macintosh HD", "-role", "S",
        "-groupWith", data["DeviceIdentifier"], "-nomount")
    run(DISKUTIL, "apfs", "deleteVolume", device(seed["DeviceIdentifier"]))
    for name, role, _ in ROLES[1:]:
        run(DISKUTIL, "apfs", "addVolume", reference, "APFS", name, "-role", role, "-nomount")
    if include_update:
        name, role, _ = UPDATE_VOLUME
        run(DISKUTIL, "apfs", "addVolume", reference, "APFS", name, "-role", role, "-nomount")
    return verify_split_layout(store, include_update=include_update)


@contextmanager
def mounted_volume(entry, mountpoint, *, authorize=False, owners=False):
    dev = device(entry["DeviceIdentifier"])
    # Mount only the generated auxiliary volume. The caller may use this
    # same authorization for its Preboot blessing, never for package work
    # or prelinking. Running period kextcache as root would contact the
    # current host's unrelated kext service and fail to take its lock.
    if authorize:
        options = "nobrowse" if owners else "nobrowse,noowners"
        subprocess.run(("/usr/bin/sudo", "-p", "Administrator password to mount the scratch APFS volume: ",
                        "/sbin/mount_apfs", "-o", options, dev, mountpoint), check=True)
    else:
        run(DISKUTIL, "mount", "-mountPoint", mountpoint, dev)
    try:
        info = plist_command(DISKUTIL, "info", "-plist", mountpoint)
        if info.get("DeviceNode") != dev or info.get("VolumeUUID") != entry["APFSVolumeUUID"]:
            raise ValueError("Mounted APFS volume does not match the new disk")
        yield mountpoint
    finally:
        if authorize:
            run("/usr/bin/sudo", "-n", "/sbin/umount", mountpoint)
        else:
            run(DISKUTIL, "unmount", dev)


def apfs_extent(disk):
    primary = gpt_header(disk, 1)
    backup = gpt_header(disk, primary[2])
    entries = primary[6]
    if (backup[2] != 1 or primary[3:5] != backup[3:5] or entries != backup[6] or
            entries[:16] != EFI.bytes_le or entries[128:144] != APFS.bytes_le or any(entries[256:])):
        raise ValueError("The detached disk does not have the expected EFI and APFS partitions")
    first, last = struct.unpack_from("<QQ", entries, 160)
    if not primary[3] <= first <= last <= primary[4] or first % 8 or (last + 1) % 8:
        raise ValueError("The detached APFS partition has invalid geometry")
    offset, size = first * SECTOR, (last + 1 - first) * SECTOR
    if disk.read(offset + 32, 8) != b"NXSB\x00\x10\x00\x00":
        raise ValueError("The detached APFS container has an invalid superblock")
    return offset, size


def copy_container(disk, path, offset, size):
    # Missing sparsebundle bands read as zeros; leave those ranges as holes
    # in the destination. Flat-disk copies can also leave unused ranges sparse.
    with path.open("xb") as output:
        mapped_ranges = getattr(disk, "allocated_ranges", None)
        if isinstance(disk, RawDisk) or mapped_ranges is not None:
            ranges = mapped_ranges(offset, size) if mapped_ranges is not None else ((offset, size),)
            for begin, length in ranges:
                for start in range(begin, begin + length, 1024 * 1024):
                    data = disk.read(start, min(begin + length - start, 1024 * 1024))
                    if data.count(0) != len(data):
                        output.seek(start - offset)
                        output.write(data)
            output.truncate(size)
            return
        for band in (disk.path / "bands").iterdir():
            if not re.fullmatch(r"[0-9a-f]+", band.name):
                continue  # e.g. AppleDouble metadata on ExFAT
            origin = int(band.name, 16) * disk.band_size
            status = band.lstat()
            if not stat.S_ISREG(status.st_mode) or status.st_size > disk.band_size:
                raise ValueError("The detached disk contains an invalid sparsebundle band")
            begin, end = max(origin, offset), min(origin + status.st_size, offset + size)
            if begin >= end:
                continue
            with band.open("rb") as source:
                source.seek(begin - origin)
                output.seek(begin - offset)
                count = end - begin
                while count:
                    block = source.read(min(count, 1024 * 1024))
                    if not block:
                        raise ValueError("The detached sparsebundle changed while reading it")
                    output.write(block)
                    count -= len(block)
        output.truncate(size)


@contextmanager
def checker_scratch(parent, size):
    # Older fsck_apfs falls back to the regular file's st_blksize after disk
    # discovery fails. ExFAT can report 1 MiB. A fresh host-native APFS
    # scratch volume gives it 4 KiB and supports holes even on an ExFAT drive.
    if parent.stat().st_blksize == 4096:
        with tempfile.TemporaryDirectory(prefix=".apfs-check-", dir=parent) as folder:
            yield Path(folder)
        return

    work = Path(tempfile.mkdtemp(prefix=".apfs-check-", dir=parent))
    backing = work / "Check.sparsebundle"
    mount = Path(tempfile.mkdtemp(prefix="vim-apfs-check-"))
    try:
        capacity = size + max(size // 16, 512 * 2**20)
        run(HDI, "create", "-sectors", str((capacity + 511) // 512), "-type", "SPARSEBUNDLE", "-layout", "GPTSPUD",
            "-fs", "APFS", "-volname", "Virtual Intel Mac Verification", backing)
        run(HDI, "attach", "-nobrowse", "-noautoopen", "-owners", "off", "-mountpoint", mount, backing)
        if mount.stat().st_dev == mount.parent.stat().st_dev:
            raise ValueError("The scratch verification volume was not mounted")
        yield mount
    finally:
        # On detach failure keep the backing store; never remove a live disk.
        for match in attached_images(backing):
            whole, = (entry["dev-entry"] for entry in match["system-entities"]
                      if entry.get("content-hint") == "GUID_partition_scheme")
            run(HDI, "detach", whole)
        mount.rmdir()
        shutil.rmtree(work)


def check_with_period_fsck(image, base):
    # The raw-device version of this check panicked macOS 27.2 twice.
    # Never attach the guest disk or pass a device node to fsck_apfs here.
    if attached_images(image):
        raise ValueError("Detach the guest disk before checking its APFS container")
    disk = SparseBundle(image)
    offset, size = apfs_extent(disk)
    with checker_scratch(image.parent, size) as folder:
        raw = folder / "Container.raw"
        copy_container(disk, raw, offset, size)
        status = raw.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_blksize != 4096:
            raise ValueError("The period APFS checker requires an ordinary 4 KiB I/O file")
        run("/usr/bin/arch", "-x86_64", base / RESOURCES / "fsck_apfs", "-n", raw)
