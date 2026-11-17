"""Qualify a stock-host APFS seed with an original Intel EFI Jumpstart.

Only newly created scratch images are formatted. Use the stock host tools
for that work; the Intel driver is data from guest media, never executed
on the host. This does not yet qualify the seed against any guest kernel.
"""

import argparse
from pathlib import Path
import struct
import tempfile

from host_install_apfs import (DISKUTIL, HDI, SECTOR, apfs_extent, attached_gpt, attached_images,
                               checker_scratch, container_info, copy_container, device,
                               read_efi_jumpstart, run)
from host_install_apfs_checkpoint import Checkpoint, SpaceMap, checksummed, write_bundle_blocks
from host_install_apfs_free_queue import retirement_checkpoint
from host_install_apfs_records import APFS, BLOCK, detached_image, u32, u64


JUMPSTART_FIELD = 0x4f8
JUMPSTART_OBJECT = 0x40000014
SEED_NAME = "Virtual Intel Mac APFS Seed"


def jumpstart_transaction(container, driver):
    pe = u32(driver, 0x3c) if len(driver) >= 64 else 0
    if (not 0 < len(driver) <= 16 * 2**20 or not driver.startswith(b"MZ") or not 64 <= pe <= len(driver) - 94
            or driver[pe:pe + 6] != b"PE\0\0\x64\x86" or driver[pe + 24:pe + 26] != b"\x0b\x02"
            or driver[pe + 92:pe + 94] != b"\x0b\0"):
        raise ValueError("Expected an original Intel APFS EFI boot service driver")
    if u64(container.superblock, JUMPSTART_FIELD):
        raise ValueError("The APFS seed already contains an EFI Jumpstart")
    checkpoint = Checkpoint(container)
    driver_blocks = (len(driver) + BLOCK - 1) // BLOCK
    allocation = SpaceMap(checkpoint).allocation(1 + driver_blocks)
    start = allocation.start
    record = bytearray(BLOCK)
    struct.pack_into("<QQII4sIII", record, 8, start, checkpoint.xid, JUMPSTART_OBJECT, 0,
                     b"JSDR", 1, len(driver), 1)
    struct.pack_into("<QQ", record, 176, start + 1, driver_blocks)
    writes = {start: checksummed(record)}
    for index in range(driver_blocks):
        offset = index * BLOCK
        writes[start + 1 + index] = driver[offset:offset + BLOCK].ljust(BLOCK, b"\0")
    updated = bytearray(container.superblock)
    struct.pack_into("<Q", updated, JUMPSTART_FIELD, start)
    writes.update(retirement_checkpoint(checkpoint, allocation, superblock=updated))
    return writes


def format_native_seed(image, size_gib, *, flat=False):
    """Create an empty, stock-format seed in the caller's private scratch job."""
    if image.exists() or image.is_symlink():
        raise FileExistsError(image)
    run(HDI, "create", "-size", f"{size_gib}g", "-type", "UDIF" if flat else "SPARSEBUNDLE", "-layout", "GPTSPUD",
        "-fs", "Journaled HFS+", "-volname", SEED_NAME, image)
    with attached_gpt(image) as (whole, entries):
        partition, = (entry for entry in entries if entry.get("content-hint") == "Apple_HFS")
        store = whole + "s2"
        if partition["dev-entry"] != device(store):
            raise ValueError("Unexpected scratch GPT geometry")
        # This destroys only the newly created empty staging slice. Use the
        # attached-image identity and its exact child, never a caller's device.
        run(DISKUTIL, "apfs", "createContainer", device(store))
        info = container_info(store)
        if info["Volumes"]:
            raise ValueError("Expected an empty new APFS container")
        run(DISKUTIL, "apfs", "addVolume", device(info["ContainerReference"]), "APFS",
            SEED_NAME, "-nomount")
    disk = detached_image(image)
    if flat and disk.size != size_gib * 2**30:
        raise ValueError("The stock raw APFS seed does not match its requested size")
    container = APFS(disk)
    volume, = container.volumes()
    if volume.name != SEED_NAME or volume.role or volume.snapshot_count:
        raise ValueError("Expected a newly created APFS seed volume")


def publish_native_jumpstart(image, driver):
    """Write Jumpstart after all native mounts of this candidate are closed."""
    if attached_images(image):
        raise ValueError("Detach the scratch APFS seed before adding its EFI driver")
    disk = detached_image(image)
    container = APFS(disk)
    write_bundle_blocks(disk, container.offset, jumpstart_transaction(container, driver))
    final = APFS(disk)
    offset, size = apfs_extent(disk)
    if (final.xid != u64(container.superblock, 96) or
            read_efi_jumpstart(disk, offset // SECTOR, (offset + size) // SECTOR - 1) != driver):
        raise ValueError("The saved APFS seed does not contain the expected Intel driver")
    return final


def create_native_seed(image, size_gib, driver):
    format_native_seed(image, size_gib)
    final = publish_native_jumpstart(image, driver)
    # No host raw device, no attaching the edited guest APFS, and no old
    # checker binary. The stock checker sees a detached regular-file copy.
    with checker_scratch(image.parent, final.blocks * BLOCK) as scratch:
        raw = scratch / "Container.raw"
        copy_container(final.image, raw, final.offset, final.blocks * BLOCK)
        run("/sbin/fsck_apfs", "-n", "-W", raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scratch", type=Path, help="Existing scratch directory")
    parser.add_argument("driver", type=Path, help="Intel apfs.efi extracted from original guest media")
    parser.add_argument("--size", type=int, default=20, help="Virtual disk size in GiB")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="VIM-NativeAPFS.", dir=args.scratch))
    output = work / "Guest.sparsebundle"
    create_native_seed(output, args.size, args.driver.read_bytes())
    print(output)


if __name__ == "__main__":
    main()
