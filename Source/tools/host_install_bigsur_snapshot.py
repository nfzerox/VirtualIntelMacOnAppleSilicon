"""Assemble and check an original Apple Intel seal on a detached Big Sur disk."""

import argparse
from contextlib import ExitStack
import io
import os
from pathlib import Path
import plistlib
import shutil
import stat
import struct
import subprocess
import tempfile
from types import SimpleNamespace
from uuid import UUID, uuid4

from host_install_apfs import attached_gpt, attached_images, checker_scratch, copy_container, mounted_volume, verify_split_layout
from host_install_apfs_checkpoint import Checkpoint, SpaceMap, checksummed, write_bundle_blocks
from host_install_apfs_free_queue import retirement_checkpoint
from host_install_apfs_records import APFS, BLOCK, OID_MASK, SparseBundle, detached_image, hashed_tree, u32, u64
from host_install_bigsur_boot import CORE, intel_root_hash, stage_intel_seal_assets
from host_install_bigsur_cache_storage import require_sealed_cache_storage
from host_install_hfs import finish_machine, mount_points, setup_state
from host_install_bigsur_extents import extent_records, file_extents, fixed_node, tree_block_count, write_tree
from host_install_bigsur_fixups import inode_paths
from host_install_bigsur_media import signed_asset


OMAP, EXTENT_REF, SNAP_META, OMAP_SNAP = 11, 15, 16, 19
PHYSICAL = 0x40000000


def object_block(oid, xid, kind):
    block = bytearray(BLOCK)
    struct.pack_into("<QQII", block, 8, oid, xid, kind, 0)
    return block


def variable_leaf(address, xid, subtype, flags, rows):
    block = object_block(address, xid, PHYSICAL | 2)
    struct.pack_into("<I", block, 28, subtype)
    table = max(8, (len(rows) + 7) & ~7) * 8
    keys, values = 56 + table, BLOCK - 40
    key_used, val_used = 0, 0
    for i, (key, value) in enumerate(rows):
        val_used += len(value)
        struct.pack_into("<4H", block, 56 + 8 * i, key_used, len(key), val_used, len(value))
        block[keys + key_used:keys + key_used + len(key)] = key
        block[values - val_used:values - val_used + len(value)] = value
        key_used += len(key)
    struct.pack_into("<HHI8H", block, 32, 3, 0, len(rows), 0, table, key_used,
                     values - keys - key_used - val_used, 0xffff, 0, 0xffff, 0)
    struct.pack_into("<6I2Q", block, values, flags, BLOCK, 0, 0,
                     max((len(k) for k, _ in rows), default=0), max((len(v) for _, v in rows), default=0),
                     len(rows), 1)
    return block


def omap_block(address, xid, root, *, flags=0, snap=0):
    block = object_block(address, xid, PHYSICAL | OMAP)
    struct.pack_into("<4I5Q", block, 32, flags, int(bool(snap)), PHYSICAL | 2, PHYSICAL | 2,
                     root, snap, xid if snap else 0, 0, 0)
    return block


def omap_record(oid, xid, address, flags=0):
    return struct.pack("<QQ", oid, xid), struct.pack("<IIQ", flags, BLOCK, address)


def allocation_runs(addresses):
    start = previous = None
    for address in sorted(addresses):
        if previous is not None and address != previous + 1:
            yield start, previous - start + 1
            start = address
        elif start is None:
            start = address
        previous = address
    if start is not None:
        yield start, previous - start + 1


class Overlay:
    def __init__(self, container, writes):
        self.container, self.writes = container, writes

    def read(self, offset, length):
        relative = offset - self.container.offset
        if relative < 0 or relative + length > self.container.blocks * BLOCK:
            return self.container.image.read(offset, length)
        data = bytearray()
        while length:
            address, start = divmod(relative, BLOCK)
            count = min(length, BLOCK - start)
            if address in self.writes:
                data += self.writes[address][start:start + count]
            else:
                data += self.container.image.read(self.container.offset + relative, count)
            relative, length = relative + count, length - count
        return bytes(data)


def assemble(container, tree, expected, remap, snapshot):
    checkpoint = Checkpoint(container)
    xid = checkpoint.xid
    system, = (volume for volume in container.volumes() if volume.role == 1)
    old = system.superblock
    old_vmap, old_nmap = u64(old, 128), u64(container.superblock, 160)
    old_pmap, old_snap = u64(old, 144), u64(old, 152)
    nmap = container.object(old_nmap, OMAP)
    if system.sealed or system.snapshot_count or u32(nmap, 36):
        raise ValueError("Expected a fresh System volume in a container without object-map snapshots")
    nx_rows = []
    retired_nx = {old_nmap, container.object_map(old_nmap, container.xid)(u64(old, 8))}
    for node in container.tree(u64(nmap, 48), int, subtype=OMAP, fixed=True):
        retired_nx.add(node.address)
        if node.leaf:
            nx_rows.extend((entry.key, entry.value) for entry in node.entries if u64(entry.key, 0) != u64(old, 8))
    retired_sys = {old_vmap}
    retired_sys.update(node.address for node in system.nodes())
    retired_sys.update(node.address for node in container.tree(u64(container.object(old_vmap, OMAP), 48),
                                                               int, subtype=OMAP, fixed=True))
    retired_sys.update(node.address for node in container.tree(old_snap, int, subtype=SNAP_META))
    source_extents = list(extent_records(file_extents(system, remap)))

    info = plistlib.loads((tree / "TreeInfo.plist").read_bytes())
    count = info["NodeCount"]
    with (tree / "FSRoot.blocks").open("rb") as stream:
        if info["BlockSize"] != BLOCK or info["RootHash"] != expected or \
                os.fstat(stream.fileno()).st_size != count * BLOCK:
            raise ValueError("The canonical System tree does not match the signed Big Sur media")
        fsroot = stream.read()
    fext_blocks = tree_block_count(len(source_extents))
    vmap_blocks = tree_block_count(count + 2)
    nmap_blocks = tree_block_count(len(nx_rows) + 1)
    # Outside the trees: two maps, one map-snapshot tree, two APSBs, the
    # empty live extent-ref, snapshot tree, UUID record and integrity record.
    needed = count + fext_blocks * 2 + vmap_blocks + nmap_blocks + 9
    allocation = SpaceMap(checkpoint).allocation(needed)
    cursor = allocation.start
    writes = {}

    def add(block):
        nonlocal cursor
        address = cursor
        writes[address] = block
        cursor += 1
        return address

    def emit(records, subtype):
        output = io.BytesIO()
        report = write_tree(output, records, cursor, xid, subtype=subtype)
        with output.getbuffer() as data:
            for offset in range(0, len(data), BLOCK):
                add(bytes(data[offset:offset + BLOCK]))
        return report["RootBlock"]

    root_oid = u64(container.superblock, 88)
    vmap_rows = [omap_record(root_oid + i, xid, add(fsroot[i * BLOCK:(i + 1) * BLOCK]), 8)
                 for i in range(count)]
    del fsroot
    integrity_oid, snap_ext_oid = root_oid + count, root_oid + count + 1
    integrity = object_block(integrity_oid, xid, 30)
    struct.pack_into("<4IQ", integrity, 32, 2, 0, 1, 128, 0)
    integrity[128:160] = expected
    vmap_rows.append(omap_record(integrity_oid, xid, add(checksummed(integrity))))
    extension = object_block(snap_ext_oid, xid, 29)
    struct.pack_into("<IIQ16s", extension, 32, 1, 0, xid, snapshot.bytes)
    vmap_rows.append(omap_record(snap_ext_oid, xid, add(checksummed(extension))))
    frozen_fext, live_fext = emit(source_extents, 31), emit(source_extents, 31)
    vroot = emit(vmap_rows, OMAP)
    snap_omap = add(checksummed(fixed_node([(struct.pack("<Q", xid), bytes(16))], cursor, xid, 0,
                                          (1, 1), subtype=OMAP_SNAP, key_size=8, tree_flags=0x10)))
    vmap = add(checksummed(omap_block(cursor, xid, vroot, snap=snap_omap)))
    live_extent_ref = add(checksummed(variable_leaf(cursor, xid, EXTENT_REF, 0x52, [])))
    live = bytearray(old)
    struct.pack_into("<Q", live, 16, xid)
    struct.pack_into("<Q", live, 56, u64(old, 56) | 0x20)
    # The volume allocation count excludes both APSBs and the container
    # object map. The retained extent ownership remains charged to System.
    new_volume_blocks = needed - nmap_blocks - 3
    struct.pack_into("<Q", live, 88, u64(old, 88) - len(retired_sys) + new_volume_blocks)
    struct.pack_into("<QQ", live, 128, vmap, root_oid)
    struct.pack_into("<Q", live, 216, 1)
    struct.pack_into("<QQ", live, 224, u64(old, 224) + new_volume_blocks, u64(old, 232) + len(retired_sys))
    struct.pack_into("<Q", live, 0x3e8, snap_ext_oid)
    struct.pack_into("<QQ", live, 0x400, integrity_oid, live_fext)

    frozen = bytearray(live)
    struct.pack_into("<QQII", frozen, 8, cursor, xid, PHYSICAL | 13, 0)
    struct.pack_into("<Q", frozen, 128, 0)
    struct.pack_into("<QQ", frozen, 144, 0, 0)
    struct.pack_into("<Q", frozen, 0x408, frozen_fext)
    frozen_address = add(checksummed(frozen))
    inum = u64(old, 176)
    name = b"com.apple.os.update-" + expected.hex().upper().encode("ascii") + b"\0"
    stamp = u64(old, 256)
    snapshot_rows = [
        (struct.pack("<Q", (1 << 60) | xid),
         struct.pack("<5QIIH", old_pmap, frozen_address, stamp, stamp, inum,
                     PHYSICAL | 2, 0, len(name)) + name),
        (struct.pack("<QH", (11 << 60) | OID_MASK, len(name)) + name, struct.pack("<Q", xid)),
    ]
    snap_root = add(checksummed(variable_leaf(cursor, xid, SNAP_META, 0x52, snapshot_rows)))
    struct.pack_into("<QQ", live, 144, live_extent_ref, snap_root)
    struct.pack_into("<Q", live, 176, inum + 1)
    # Native boot policy marks the snapshot as the root to mount.
    struct.pack_into("<Q", live, 0x3c8, xid)
    nx_rows.append(omap_record(u64(old, 8), xid, add(checksummed(live))))
    nx_rows.sort(key=lambda row: struct.unpack("<QQ", row[0]))
    nx_root = emit(nx_rows, OMAP)
    new_nmap = add(checksummed(omap_block(cursor, xid, nx_root, flags=u32(nmap, 32))))
    if cursor != allocation.start + allocation.count:
        raise ValueError("Big Sur transaction did not fit its APFS reservation")
    nx = bytearray(container.superblock)
    struct.pack_into("<Q", nx, 88, snap_ext_oid + 1)
    struct.pack_into("<Q", nx, 160, new_nmap)
    writes.update(retirement_checkpoint(checkpoint, allocation, allocation_runs(retired_sys | retired_nx),
                                        superblock=nx))
    # The reader follows the actual new checkpoint and physical OMAP. Hash
    # every placed node starting at Apple's media digest, not at a receipt.
    readback = APFS(Overlay(container, writes))
    sealed, = (v for v in readback.volumes() if v.role == 1)
    identity, _, authenticated = sealed.authenticated_snapshot(expected)
    if identity != snapshot or sum(1 for _ in authenticated) != count:
        raise ValueError("Incomplete Big Sur sealed snapshot")
    return writes


def stage_boot_selector(image, snapshot, selector, seal_assets):
    selector.write_text(str(snapshot).upper(), encoding="ascii")
    with attached_gpt(image) as (whole, _), mount_points() as (_, mount):
        volumes, group = verify_split_layout(whole + "s2", include_update=True)
        with mounted_volume(volumes["Preboot"], mount, authorize=True, owners=True):
            core = mount / group / CORE
            if not (core / "boot.efi").is_file():
                raise ValueError("The detached Big Sur seed lacks the group's Intel boot EFI")
            sudo = ("/usr/bin/sudo", "-n")
            if seal_assets:
                tree = mount / group
                subprocess.run((*sudo, "/usr/bin/install", "-d", "-o", "0", "-g", "0", "-m", "0755",
                                tree / "usr/standalone"), check=True)
                for file in sorted(seal_assets.rglob("*")):
                    if not file.is_file():
                        continue
                    destination = tree / file.relative_to(seal_assets)
                    subprocess.run((*sudo, "/usr/bin/install", "-o", "0", "-g", "0", "-m", "0644",
                                    file, destination), check=True)
            subprocess.run((*sudo, "/usr/bin/install", "-o", "0", "-g", "0", "-m", "0644",
                            selector, core / ".root_uuid"), check=True)
            subprocess.run((*sudo, "/usr/sbin/bless", "--folder", core, "--file", core / "boot.efi"), check=True)


def verify_detached(container, writes, *, software_keybags=False):
    disk = container.image
    if software_keybags and writes:
        raise ValueError("Decode keybags only on an already published disk audit")
    if software_keybags:
        from host_install_bigsur_keybags import decoded_keybags
        writes = decoded_keybags(container)
    with checker_scratch(disk.path.parent, container.blocks * BLOCK) as folder:
        raw = folder / "SealedContainer.raw"
        copy_container(disk, raw, container.offset, container.blocks * BLOCK)
        status = raw.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_blksize != BLOCK:
            raise ValueError("The APFS checker requires an ordinary scratch file")
        with raw.open("r+b") as stream:
            for address, block in writes.items():
                if os.pwrite(stream.fileno(), block, address * BLOCK) != BLOCK:
                    raise OSError("Short write to APFS verification copy")
        command = ("/sbin/fsck_apfs", "-n", "-W")
        if software_keybags and writes:
            # Select the checker's raw file read, not the IOKit lookup for
            # a live device UUID. All volumes are unencrypted, so no volume
            # password is required. Only the disposable file holds plaintext
            # keybag objects; the original ciphertext was verified above.
            subprocess.run((*command, "-r", "0", raw), input=b"", check=True)
        else:
            subprocess.run((*command, raw), check=True)
    return len(writes) if software_keybags else 0


def media_digest(package, seal_assets=None):
    with signed_asset(package) as (_, archive):
        root, = (n for n in archive.namelist() if n.endswith(".x86.root_hash"))
        expected = intel_root_hash(archive.read(root))
        if seal_assets is not None:
            stage_intel_seal_assets(archive, root, seal_assets)
        return expected


def verify_bigsur_boot_files(container, expected):
    """Read the sealed boot chain and prepared Data without mounting either."""
    volumes = list(container.volumes())
    if sorted(volume.role for volume in volumes) != [1, 4, 8, 16, 64, 192]:
        raise ValueError("The detached Big Sur disk does not have the expected six APFS roles")
    roles = {volume.role: volume for volume in volumes}
    system, preboot, data = (roles[role] for role in (1, 16, 64))
    group = str(data.uuid).upper()
    account = plistlib.loads(data.read_small_file(os.fsencode(setup_state("bigsur") / "Account.plist")))
    identity = UUID(account["MachineIdentifier"])
    if account.get("Guest") != "bigsur" or account.get("VolumeUUID") != group or \
            system.superblock[0x3f0:0x400] != data.uuid.bytes:
        raise ValueError("The Big Sur account, System and Data must belong to the same prepared VM")
    snapshot = str(system.signed_root_snapshot(expected)).upper().encode("ascii")
    require_sealed_cache_storage(system, expected)
    if preboot.read_small_file(os.fsencode(f"{group}/{CORE}.root_uuid")) != snapshot:
        raise ValueError("Preboot must select the prepared Big Sur snapshot")
    if intel_root_hash(preboot.read_small_file(os.fsencode(
            f"{group}/usr/standalone/OS.dmg.root_hash"))) != expected:
        raise ValueError("Preboot's root hash does not match the signed Big Sur installer")
    staged_paths = inode_paths(data)
    for name, identifier in (("AppleParavirtGPUMetal", "com.apple.driver.AppleParavirtGPUMetal"),
                             ("AppleMetalOpenGLRenderer", "com.apple.AppleMetalOpenGLRenderer")):
        contents = f"Library/GPUBundles/{name}.bundle/Contents"
        info = plistlib.loads(data.read_small_file(os.fsencode(contents + "/Info.plist")))
        if info.get("CFBundleIdentifier") != identifier or info.get("CFBundleExecutable") != name or \
                os.fsencode(f"{contents}/MacOS/{name}") not in staged_paths:
            raise ValueError(f"The Big Sur Data volume lacks the prepared {name} driver")
    return account, identity, group, snapshot.decode("ascii")


def export_bigsur_machine(image, app, output, expected, ssh_port):
    """Export the authenticated, prepared disk to the app's ordinary VM format."""
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    if attached_images(image) or not 1024 <= ssh_port <= 65535:
        raise ValueError("Use a detached Big Sur disk and an unprivileged SSH port")
    disk = detached_image(image)
    account, identity, group, snapshot = verify_bigsur_boot_files(APFS(disk), expected)
    size, remainder = divmod(disk.size, 2**30)
    if remainder:
        raise ValueError("Big Sur VM disk size must be a whole number of GiB")
    runtime = app / "Contents/Resources/Runtime"
    with tempfile.TemporaryDirectory(prefix=".vim-bigsur-vm-", dir=output.parent) as work:
        stage = Path(work) / "Machine"
        stage.mkdir(mode=0o700)
        for directory in ("Disks", "Firmware", "State", "Logs", "Installation"):
            (stage / directory).mkdir(mode=0o700)
        args = SimpleNamespace(guest="bigsur", size=size, ssh=bool(account.get("RemoteLogin")), ssh_port=ssh_port)
        finish_machine(args, runtime, output, stage, image, str(identity).upper(), account["Build"],
                       group, ("Apple Intel System",), filesystem="APFS", detached=True, keep_stage_image=True,
                       assembly_details={"BigSurGraphicsStaged": True, "BootSnapshotUUID": snapshot})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="New, detached Big Sur sparsebundle (read-only)")
    parser.add_argument("package", type=Path, help="Original Apple-signed Big Sur InstallAssistant")
    parser.add_argument("tree", type=Path, help="Canonical System blocks derived from the signed media")
    parser.add_argument("remap", type=Path, help="Canonical object IDs derived from Apple's mtree")
    parser.add_argument("--output-image", type=Path, help="Publish a new verified sparsebundle; authorize only its Preboot mount")
    parser.add_argument("--app", type=Path, help="Virtual Intel Mac app supplying the guest runtime and firmware")
    parser.add_argument("--vm-output", type=Path, help="Also export a normal VM bundle")
    parser.add_argument("--ssh-port", type=int, default=23226)
    args = parser.parse_args()
    if (args.app is None) != (args.vm_output is None) or (args.vm_output and not args.output_image):
        parser.error("--app and --vm-output must be used together with --output-image")
    if args.vm_output and (args.vm_output.exists() or args.vm_output.is_symlink()):
        raise FileExistsError(args.vm_output)
    if attached_images(args.image):
        raise ValueError("Detach the Big Sur scratch image first")
    remap = {int(key): oid for key, oid in plistlib.loads(args.remap.read_bytes()).items() if key != "CHANGE"}
    snapshot = uuid4()
    if args.output_image is None:
        expected = media_digest(args.package)
        container = APFS(SparseBundle(args.image))
        writes = assemble(container, args.tree, expected, remap, snapshot)
        print(f"Authenticated Big Sur snapshot {snapshot}; checking detached APFS copy.", flush=True)
        verify_detached(container, writes)
        return
    if args.output_image.exists() or args.output_image.is_symlink():
        raise FileExistsError(args.output_image)
    work = Path(tempfile.mkdtemp(prefix=".vim-bigsur-seal-", dir=args.output_image.parent))
    seed = work / "BigSur.sparsebundle"
    try:
        assets = work / "SealAssets"
        expected = media_digest(args.package, assets)
        print("Preparing a separate Big Sur disk and staging its snapshot boot selector.", flush=True)
        shutil.copytree(args.image, seed)
        stage_boot_selector(seed, snapshot, work / "root_uuid", assets)
        if attached_images(seed):
            raise ValueError("The new Big Sur scratch disk is still attached")
        container = APFS(SparseBundle(seed))
        writes = assemble(container, args.tree, expected, remap, snapshot)
        write_bundle_blocks(container.image, container.offset, writes)
        print(f"Authenticated Big Sur snapshot {snapshot}; checking detached APFS copy.", flush=True)
        verify_detached(container, {})
        persisted = APFS(container.image)
        preboot, = (volume for volume in persisted.volumes() if volume.role == 16)
        system, = (volume for volume in persisted.volumes() if volume.role == 1)
        group = str(UUID(bytes=system.superblock[0x3f0:0x400])).upper()
        selector = preboot.read_small_file(os.fsencode(f"{group}/{CORE}.root_uuid"))
        if selector != str(system.signed_root_snapshot(expected)).upper().encode("ascii"):
            raise ValueError("The Big Sur Preboot selector does not identify its sealed snapshot")
        if args.output_image.exists():
            raise FileExistsError(args.output_image)
        seed.rename(args.output_image)
        print(args.output_image)
    finally:
        if not seed.exists() or not attached_images(seed):
            shutil.rmtree(work)
    if args.vm_output:
        export_bigsur_machine(args.output_image, args.app, args.vm_output, expected, args.ssh_port)


if __name__ == "__main__":
    main()
