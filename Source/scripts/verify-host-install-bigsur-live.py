#!/usr/bin/env python3
"""Verify a running, host-assembled Big Sur guest through the real app.

Launch a fresh bundle once with --guest-diagnostics DIRECTORY and wait for its
graphics-verified receipt. Use a new evidence path outside the VM bundle. This
records the live checks; separately observe the app's presented desktop and
whether this was the first launch. It does not boot or modify the guest.
For a subsequent normal boot, pass the first live evidence via --previous.
"""

import argparse
import json
from pathlib import Path
import plistlib
import re
import subprocess
import sys
from uuid import UUID


APP_COMMAND = Path(__file__).with_name("app-guest-command.py")
VERIFIER = "/Library/Application Support/Virtual Intel Mac/host-assembly-verify"
LIVE_COMMAND = '''set -eu
printf 'Build: '; /usr/bin/sw_vers -buildVersion
printf 'Boot session: '; /usr/sbin/sysctl -n kern.bootsessionuuid
printf 'Console: '; /usr/bin/stat -f '%Su %u' /dev/console
/sbin/mount | /usr/bin/grep ' on / ('
/usr/sbin/diskutil info /
/usr/sbin/diskutil apfs list
/usr/bin/csrutil authenticated-root status
/usr/sbin/ioreg -r -c IOAccelerator -d 1 | /usr/bin/grep 'IOGLBundleName'
'''


def check(condition, message):
    if not condition:
        raise ValueError(message)


def uuid(value):
    return UUID(value) if isinstance(value, str) else None


def fields(text, pattern):
    return re.findall(pattern, text, re.MULTILINE)


def check_live(assembly, configuration, username, report, stdout, blocks, disk, *, previous=None, metal=None):
    proof = assembly["GraphicsVerification"]
    build, first_boot, group, snapshot = (assembly["ProductBuildVersion"], uuid(proof["BootSessionUUID"]),
                                           uuid(assembly["VolumeUUID"]), uuid(assembly["BootSnapshotUUID"]))
    check(assembly["Phase"] == "graphics-verified" and first_boot and group and snapshot and
          assembly["MachineIdentifier"] == proof["MachineIdentifier"] == configuration["Identifier"] and
          proof["MetalCommandVerified"] is True and proof["ProductBuildVersion"] == build and
          uuid(proof["VolumeUUID"]) == group, "The app has no matching Metal command/readback receipt")
    boot = uuid(report["BootSessionUUID"])
    if previous is not None:
        check(previous["Assembly"]["GraphicsVerification"] == proof and
              uuid(previous["Assembly"]["BootSnapshotUUID"]) == snapshot and
              uuid(previous["LiveReport"]["BootSessionUUID"]) == first_boot and boot != first_boot,
              "Expected a new boot of the same previously verified assembly and snapshot")
        check(isinstance(metal, dict) and metal.get("MetalCommandVerified") is True and
              metal.get("ProductBuildVersion") == build and uuid(metal.get("BootSessionUUID")) == boot and
              uuid(metal.get("VolumeUUID")) == group, "Live Metal readback did not pass on the new boot")
    else:
        check(boot == first_boot, "The live Intel desktop does not match the app's first-boot receipt")
    # The guest's Objective-C report can box a C boolean expression as 1.
    check(report["Guest"] == "bigsur" and all(report.get(flag) == 1 for flag in
          ("InstalledSystemCandidate", "KernelMatchesSystem", "DesktopRunning", "LiveRoot")) and
          report["ProcessMachine"] == "x86_64" and report["ProductBuildVersion"] == build and
          uuid(report["VolumeUUID"]) == group and uuid(report["BootSessionUUID"]) == boot,
          "The live Intel desktop does not match the app's graphics receipt")
    check(fields(stdout, r"^Build: (\S+)\s*$") == [build] and
          [uuid(value) for value in fields(stdout, r"^Boot session: (\S+)\s*$")] == [boot],
          "The guest restarted or does not match the installed build")
    uid = report["ConsoleUID"]
    check(isinstance(uid, int) and uid >= 501 and
          fields(stdout, r"^Console: (\S+ \d+)\s*$") == [f"{username} {uid}"],
          "The requested account is not logged in at the guest console")
    mounts = fields(stdout, r"^[^\n]+ on / \(([^\n]+)\)$")
    check(len(mounts) == 1 and {"apfs", "sealed", "read-only"} <= set(mounts[0].split(", ")) and
          [uuid(value) for value in fields(stdout, r"^[ \t]+APFS Snapshot UUID:[ \t]+(\S+)$")] == [snapshot] and
          [uuid(value) for value in fields(stdout, r"^[ \t]+APFS Volume Group:[ \t]+(\S+)$")] == [group],
          "The running guest did not mount the expected read-only sealed System snapshot")
    # The unmounted live System volume may show Broken while the actual
    # root snapshot remains sealed. Match the seal to that exact UUID.
    seals = fields(stdout, r"^[ |\t]+Snapshot:[ \t]+(\S+)\n(?:^[ |\t]+Snapshot [^\n]+\n)*?"
                           r"^[ |\t]+Snapshot Sealed:[ \t]+(\S+)$")
    check([(uuid(value), seal) for value, seal in seals if uuid(value) == snapshot] == [(snapshot, "Yes")] and
          "Authenticated Root status: enabled" in stdout.splitlines(),
          "The mounted snapshot is not sealed with authenticated root enabled")
    check('"IOGLBundleName" = "AppleMetalOpenGLRenderer"' in stdout,
          "The Apple PVG OpenGL renderer is not registered")
    check(isinstance(blocks, list) and not any(block.get("removable") and block.get("inserted") for block in blocks) and
          any(Path(path).resolve() == disk for block in blocks if (inserted := block.get("inserted"))
              if (path := inserted.get("image", {}).get("filename"))),
          "The running VM must have its own System disk and no inserted removable media")
    if previous is not None:
        def attached_file(report, device):
            return [Path(name).resolve() for block in report if block.get("device") == device
                    if (name := block.get("inserted", {}).get("image", {}).get("filename"))]

        for device in ("macos", "pflash1"):
            original = attached_file(previous["Blocks"], device)
            check(len(original) == 1 and original == attached_file(blocks, device),
                  f"The next boot did not reuse the original {device} file")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("machine", type=Path)
    parser.add_argument("diagnostics", type=Path)
    parser.add_argument("--username", required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--previous", type=Path, help="The first-boot evidence, to verify a new boot of that same disk")
    args = parser.parse_args()
    if args.evidence.exists() or args.evidence.is_symlink() or args.evidence.resolve().is_relative_to(args.machine.resolve()):
        parser.error("Choose a new evidence file outside the VM bundle")

    def request(*command):
        output = subprocess.check_output((sys.executable, APP_COMMAND, args.diagnostics, *command))
        return json.loads(output)

    try:
        configuration = plistlib.loads((args.machine / "Machine.plist").read_bytes())
        assembly = plistlib.loads((args.machine / "State/HostAssembly.plist").read_bytes())
        check(configuration.get("Guest") == "bigsur" and configuration.get("InstallState") == "installed" and
              not configuration.get("InstallerMedia"), "Use an installed Big Sur bundle with no installer")
        check(assembly.get("Phase") == "graphics-verified", "Wait for the app's live Metal verification")
        report = request("--status")["Report"]
        stdout = request("/bin/sh", "-c", LIVE_COMMAND)["Stdout"]
        blocks = request("--inspect", "media")["Report"]
        previous = json.loads(args.previous.read_bytes()) if args.previous else None
        metal = json.loads(request(VERIFIER, "--verify-pvg-device")["Stdout"]) if previous else None
        check_live(assembly, configuration, args.username, report, stdout, blocks,
                   (args.machine / configuration["Disk"]).resolve(), previous=previous, metal=metal)
        evidence = {"Assembly": assembly, "LiveReport": report, "GuestOutput": stdout, "Blocks": blocks}
        if previous:
            evidence["FollowUpMetal"] = metal
        with args.evidence.open("x") as file:
            json.dump(evidence, file, indent=2)
            file.write("\n")
    except (KeyError, OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Big Sur live qualification did not pass: {error}") from error
    print(f"Big Sur desktop, Apple PVG readback and authenticated System snapshot verified: {args.evidence}")


if __name__ == "__main__":
    main()
