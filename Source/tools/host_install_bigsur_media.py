"""Inspect the signed Big Sur installer before assembling a new guest disk."""

import argparse
from contextlib import contextmanager
import json
import lzma
import os
from pathlib import Path
import plistlib
import shutil
import stat
import struct
import subprocess
import sys
import zipfile

from host_install_apfs import HDI
from host_install_hfs import mount_points


ASSET = "com_apple_MobileAsset_MacSoftwareUpdate"
PAYLOAD = "AssetData/payloadv2/"
DYLD = b"System/Library/dyld/dyld_shared_cache_"
ACCOUNT_TEMPLATE = Path("System/Library/Templates/Data/private/var/db/dslocal/nodes/Default")


@contextmanager
def signed_asset(package):
    result = subprocess.check_output(("/usr/sbin/pkgutil", "--check-signature", package),
                                     stdin=subprocess.DEVNULL, env={**os.environ, "LC_ALL": "C"})
    if b"Status: signed Apple Software" not in [line.strip() for line in result.splitlines()]:
        raise ValueError("Big Sur requires the original Apple-signed InstallAssistant package")
    with mount_points() as (mount, _):
        subprocess.run((HDI, "attach", "-readonly", "-nobrowse", "-noautoopen", "-noverify", "-mountpoint", mount, package),
                       stdin=subprocess.DEVNULL, stdout=sys.stderr, check=True)
        try:
            source = mount / ASSET
            manifest = plistlib.loads((source / f"{ASSET}.xml").read_bytes())
            asset, = manifest["Assets"]
            archive, = source.glob("*.zip")
            with zipfile.ZipFile(archive) as payload:
                yield asset, payload
        finally:
            subprocess.run((HDI, "detach", mount), stdin=subprocess.DEVNULL, stdout=sys.stderr, check=True)


def decode_pbzx(stream):
    header = stream.read(12)
    if len(header) != 12 or not header.startswith(b"pbzx"):
        raise ValueError("Expected a Big Sur PBZX payload chunk")
    maximum, = struct.unpack(">Q", header[4:])
    while block := stream.read(16):
        if len(block) != 16:
            raise ValueError("Truncated Big Sur PBZX block header")
        size, stored_size = struct.unpack(">QQ", block)
        if not 0 < stored_size <= size <= maximum <= 16 * 2**20:
            raise ValueError("Unexpected Big Sur PBZX block geometry")
        stored = stream.read(stored_size)
        if len(stored) != stored_size:
            raise ValueError("Truncated Big Sur PBZX block")
        decoded = lzma.decompress(stored) if stored.startswith(b"\xfd7zXZ\x00") else stored
        if len(decoded) != size:
            raise ValueError("Unexpected decoded Big Sur PBZX block size")
        yield decoded


def first_record_prefix(stream):
    decoded = next(decode_pbzx(stream), b"")
    if not decoded.startswith(b"YAA1"):
        raise ValueError("The Big Sur payload does not start with an Apple Archive record")
    return decoded


def inspect_asset(asset, archive):
    info = plistlib.loads(archive.read("AssetData/Info.plist"))
    version, build = info["ProductVersion"], info["Build"]
    if not version.startswith("11.") or (version, build) != (asset["OSVersion"], asset["Build"]):
        raise ValueError("The signed Big Sur package and its payload metadata disagree")
    members = archive.namelist()
    chunks, caches = [], {}
    for line in archive.read(PAYLOAD + "payload_chunks.txt").decode("ascii").splitlines():
        part, byte_count = map(int, line.split(":"))
        member = PAYLOAD + f"payload.{part:03}"
        if part != len(chunks) or byte_count <= 0 or members.count(member) != 1:
            raise ValueError("Unexpected Big Sur payload sequence")
        with archive.open(member) as stream:
            prefix = first_record_prefix(stream)
        # Apple puts each large cache at the beginning of its own chunk.
        # Record only cache entries whose path and Mach-O cache header agree.
        header_length = struct.unpack_from("<H", prefix, 4)[0]
        if not 6 <= header_length <= len(prefix):
            raise ValueError("Invalid Big Sur Apple Archive header length")
        for architecture in (b"x86_64", b"x86_64h"):
            if (prefix[:header_length].find(DYLD + architecture) >= 0 and
                    prefix[header_length:header_length + 16].split() == [b"dyld_v1", architecture + b"\0"]):
                caches[architecture.decode()] = member
        # This is Apple's estimated change in target disk usage, not the
        # length of the PBZX stream (e.g. .000 is only ~32 MiB decoded).
        chunks.append({"Member": member, "TargetSpaceHint": byte_count})
    if set(caches) != {"x86_64", "x86_64h"}:
        raise ValueError("The full Big Sur payload is missing the original Intel dyld caches")
    links = subprocess.check_output(("/usr/bin/aa", "list", "-list-format", "json"),
                                    input=archive.read(PAYLOAD + "firmlinks_payload"))
    hashes = [n for n in members if n.startswith("AssetData/boot/Firmware/") and
              n.endswith(".x86.root_hash")]
    if len(hashes) != 1:
        raise ValueError("The Big Sur Intel root-hash member is missing or ambiguous")
    return {"Version": version, "Build": build, "Chunks": chunks, "SharedCaches": caches,
            "FirmlinkStubs": [entry["PAT"] for entry in json.loads(links)], "IntelRootHashes": hashes}


def run_archive(archive, member, *arguments, output=None, authorize=False, decoder=None,
                decoder_arguments=(), extractor="/usr/bin/aa"):
    """Stream a ZIP member through its decoder to Apple's native extractor."""
    prefix = ("/usr/bin/sudo", "-n") if authorize else ()
    command = (*prefix, extractor, *arguments)
    with archive.open(member) as data, subprocess.Popen(command, stdin=subprocess.PIPE,
                                                       stdout=output if output is not None else sys.stderr) as process:
        if decoder is not None:
            # Forward the compressed ZIP member to the universal decoder;
            # decoded bytes flow straight into the native archive tool.
            with subprocess.Popen((decoder, *decoder_arguments), stdin=subprocess.PIPE,
                                  stdout=process.stdin) as decode:
                process.stdin.close()
                try:
                    with decode.stdin as sink:
                        shutil.copyfileobj(data, sink, 1024 * 1024)
                except BrokenPipeError:
                    pass  # Report the actual worker/consumer failure below.
                except BaseException:
                    for child in (decode, process):
                        if child.poll() is None:
                            child.terminate()
                    raise
                if result := decode.wait():
                    raise ValueError(f"Native archive decoding failed for {member} (status {result})")
            if result := process.wait():
                raise subprocess.CalledProcessError(result, command)
            return
        try:
            with process.stdin as pipe:
                for block in decode_pbzx(data):
                    pipe.write(block)
        except BaseException:
            if process.poll() is None:
                process.terminate()
            raise
        if result := process.wait():
            raise subprocess.CalledProcessError(result, command)


def apply_main_chunks(archive, root, chunks, *, preserve_metadata=False, decoder=None):
    account_template = root / ACCOUNT_TEMPLATE
    original_mode = None
    try:
        for number, item in enumerate(chunks, 1):
            if original_mode is None and account_template.is_dir():
                mode = stat.S_IMODE(account_template.stat().st_mode)
                if not mode & stat.S_IXUSR:
                    # This template is 0600 in Apple's archive. Later chunks
                    # still contain members under it, so permit traversal only
                    # while the tool extracts into our unpublished scratch.
                    set_staging_mode(account_template, mode | stat.S_IXUSR, preserve_metadata)
                    original_mode = mode
            print(f"Reading original Big Sur System files ({number}/{len(chunks)})", file=sys.stderr, flush=True)
            # Never apply protected flags through the running host's VFS.
            # The owned APFS target can retain archive UID, GID and ACL
            # directly; the unprivileged inventory tree cannot.
            exclusions = "flg" if preserve_metadata else "uid,gid,flg,acl"
            options = {"decoder": decoder} if decoder is not None else {}
            if preserve_metadata:
                run_archive(archive, item["Member"], "extract", "-d", root, "-exclude-field", exclusions,
                            "-no-ignore-eperm", authorize=True, **options)
            else:
                run_archive(archive, item["Member"], "extract", "-d", root, "-exclude-field", exclusions, **options)
    finally:
        if original_mode is not None:
            set_staging_mode(account_template, original_mode, preserve_metadata)


def set_staging_mode(path, mode, authorize):
    if authorize:
        subprocess.run(("/usr/bin/sudo", "-n", "/bin/chmod", f"{mode:o}", path),
                       stdin=subprocess.DEVNULL, check=True)
    else:
        path.chmod(mode)


def verify_main_payload(root, inventory):
    installed = plistlib.loads((root / "System/Library/CoreServices/SystemVersion.plist").read_bytes())
    if installed["ProductBuildVersion"] != inventory["Build"]:
        raise ValueError("The staged Big Sur files do not match the source build")
    for architecture in inventory["SharedCaches"]:
        cache = root / f"System/Library/dyld/dyld_shared_cache_{architecture}"
        with cache.open("rb") as stream:
            if stream.read(16).split() != [b"dyld_v1", architecture.encode() + b"\0"]:
                raise ValueError(f"The staged {architecture} cache is not the original Intel cache")


def stage_main_payload(archive, inventory, directory):
    """Stage the complete original file content, not a bootable or sealed disk."""
    directory.mkdir(mode=0o700)
    image = directory / "BigSurSystem.sparsebundle"
    log = {"stdin": subprocess.DEVNULL, "stdout": sys.stderr, "check": True}
    subprocess.run((HDI, "create", "-size", "32g", "-type", "SPARSEBUNDLE", "-layout", "GPTSPUD",
                    "-fs", "APFS", "-volname", "Big Sur System Staging", image), **log)
    with mount_points() as (_, root):
        subprocess.run((HDI, "attach", "-nobrowse", "-noautoopen", "-owners", "off", "-mountpoint", root, image), **log)
        try:
            apply_main_chunks(archive, root, inventory["Chunks"])
            verify_main_payload(root, inventory)
        finally:
            subprocess.run((HDI, "detach", root), **log)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="Original Apple InstallAssistant.pkg or signed SharedSupport.dmg")
    parser.add_argument("--stage-main", type=Path, help="Extract System contents into a new scratch directory (not a VM)")
    args = parser.parse_args()
    with signed_asset(args.package) as (asset, archive):
        report = inspect_asset(asset, archive)
        if args.stage_main:
            report["StagedSystemFiles"] = str(stage_main_payload(archive, report, args.stage_main))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
