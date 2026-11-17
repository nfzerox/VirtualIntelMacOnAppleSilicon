#!/usr/bin/env python3
"""Check the Mojave native-install update overlay after repair and again after reboot.

This observes the visible app's guest diagnostics channel. It neither installs
the update nor restarts the guest. Capture the original account and marker
before letting the guest's own Software Update apply the authentic package.
"""

import argparse
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import subprocess
import sys


APP_COMMAND = Path(__file__).with_name("app-guest-command.py")
IDENTITY = '''set -eu
user=$(/usr/bin/stat -f %Su /dev/console)
home=$(/usr/bin/dscl . -read "/Users/$user" NFSHomeDirectory | /usr/bin/sed 's/^NFSHomeDirectory: //')
marker="$home/Documents/Virtual Intel Mac Update QA.txt"
printf 'Build: '; /usr/bin/sw_vers -buildVersion
printf 'Boot session: '; /usr/sbin/sysctl -n kern.bootsessionuuid
printf 'Console: '; /usr/bin/stat -f '%Su %u' /dev/console
/usr/bin/dscl . -read "/Users/$user" GeneratedUID
printf 'Marker UID: '; /usr/bin/stat -f %u "$marker"
printf 'Marker SHA-256: '; /usr/bin/shasum -a 256 < "$marker" | /usr/bin/awk '{print $1}'
'''
BASE, TARGET = "18G103", "18G9323"


def require(condition, explanation):
    if not condition:
        raise ValueError(explanation)


def identity(text):
    return dict(line.split(": ", 1) for line in text.splitlines() if ": " in line)


def check_update(machine, baseline, assembly, repair, report, live, metal, prior=None):
    require(machine["Identifier"] == assembly["MachineIdentifier"] and
            baseline["Build"] == assembly["ProductBuildVersion"] == BASE and
            live["Build"] == report["ProductBuildVersion"] == metal["ProductBuildVersion"] == TARGET,
            "The installed and running guest must advance from 18G103 to 18G9323")
    boot, volume = report["BootSessionUUID"], report["VolumeUUID"]
    require(boot == live["Boot session"] == metal["BootSessionUUID"] and boot != baseline["Boot session"] and
            volume == assembly["VolumeUUID"] == metal["VolumeUUID"], "Boot or installed volume changed during verification")
    require(all(report.get(key) == 1 for key in ("InstalledSystemCandidate", "LiveRoot", "KernelMatchesSystem", "DesktopRunning"))
            and report["Guest"] == machine["Guest"] == "mojave" and not machine.get("InstallerMedia"),
            "Expected a running installed Mojave desktop with no installer")
    require(all(live[key] == baseline[key] for key in ("Console", "GeneratedUID", "Marker UID", "Marker SHA-256")),
            "The original account, console user or its test document was not preserved")
    require(metal.get("MetalCommandVerified") is True, "Live PVG Metal command/readback did not pass")
    proof = repair["PostBootVerification"]
    require(repair["Phase"] == "graphics-verified" and proof["VerifiedAfterReboot"] is True and
            proof["MetalCommandVerified"] is True and proof["TransactionID"] == repair["TransactionID"] and
            proof["ProductBuildVersion"] == TARGET and proof["VolumeUUID"] == volume,
            "The app has no successful automatic graphics repair for this build and disk")
    if prior is None:
        require(proof["BootSessionUUID"] == boot, "First verify the boot that completed automatic graphics repair")
    else:
        # The app may refresh its read-only Metal receipt on subsequent
        # boots. The transaction must stay the same, not its proof's UUID.
        require(prior["MachineIdentifier"] == machine["Identifier"] and prior["RepairTransactionID"] == repair["TransactionID"] and
                prior["Build"] == TARGET and prior["VolumeUUID"] == volume and prior["BootSessionUUID"] != boot and
                proof["BootSessionUUID"] in (prior["BootSessionUUID"], boot),
                "A follow-up boot must be a new session after the already verified repair, without another repair")
    return {"MachineIdentifier": machine["Identifier"], "Build": TARGET, "VolumeUUID": volume,
            "BootSessionUUID": boot, "RepairTransactionID": repair["TransactionID"],
            "AccountPreserved": True, "TestDocumentPreserved": True, "MetalCommandVerified": True,
            "SubsequentNormalBoot": prior is not None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("machine", type=Path)
    parser.add_argument("diagnostics", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--previous", type=Path, help="The first verified post-update boot, for the second boot check")
    args = parser.parse_args()
    if args.evidence.resolve().is_relative_to(args.machine.resolve()) or args.evidence.exists():
        parser.error("Choose a new evidence file outside the VM bundle")

    def request(*command):
        return json.loads(subprocess.check_output((sys.executable, APP_COMMAND, args.diagnostics, *command)))

    def read(relative):
        return plistlib.loads((args.machine / relative).read_bytes())

    try:
        machine, assembly, repair = (read(path) for path in
                                     ("Machine.plist", "State/HostAssembly.plist", "State/GuestInstallation.plist"))
        baseline = json.loads(args.baseline.read_bytes())
        require(baseline["ExitCode"] == 0, "The original account and test document baseline did not complete")
        require(repair.get("Phase") == "graphics-verified", "Wait for the app to finish the automatic update repair")
        # An older host assembly may only have staged its first-boot
        # verifier temporarily. The repaired guest retains the installer
        # tool next to its transaction, which can run a fresh Metal probe.
        journal = repair["GuestJournal"]
        require(re.fullmatch(r"/private/var/db/VirtualIntelMac\.[A-Za-z0-9]+/InstallTransaction\.plist", journal) and
                journal == repair["Preparation"]["TransactionJournal"], "Invalid guest repair journal path")
        verifier = str(PurePosixPath(journal).with_name("guest-install"))
        report = request("--status")["Report"]
        live = identity(request("/bin/sh", "-c", IDENTITY)["Stdout"])
        metal = json.loads(request(verifier, "--verify-pvg-device")["Stdout"])
        proof = check_update(machine, identity(baseline["Stdout"]), assembly, repair, report, live, metal,
                             json.loads(args.previous.read_bytes()) if args.previous else None)
        with args.evidence.open("x") as output:
            json.dump(proof, output, indent=2)
            output.write("\n")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as problem:
        raise SystemExit(f"Mojave update qualification did not pass: {problem}") from problem
    print(f"Updated Mojave account, document and live Metal readback verified: {args.evidence}")


if __name__ == "__main__":
    main()
