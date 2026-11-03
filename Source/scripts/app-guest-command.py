#!/usr/bin/env python3
"""Send a bounded diagnostic request through a visible development app.

Launch the app with --guest-diagnostics ABSOLUTE_DIRECTORY and --open-vm PATH.
Then invoke this script DIRECTORY /absolute/guest/program [arguments ...].
For multiple VM windows in one app, use --guest-diagnostics-root ABSOLUTE_ROOT
instead: each session creates ROOT/MACHINE_IDENTIFIER with its own command
files. This takes precedence over --guest-diagnostics. Use that child directory
with this adapter; no second QMP/QGA socket client is ever opened.
Let the app create a nonexistent diagnostics root, or create it owner-only
(0700) on the local filesystem. Insecure roots are intentionally rejected.
Use DIRECTORY --capture to save the app's own guest frame as Frame.png.
Use DIRECTORY --status to time the read-only production guest status probe.
Use DIRECTORY --installer-log for the existing read-only installer log action.
Use DIRECTORY --cursor-state for the native cursor's visibility/glyph snapshot.
Use DIRECTORY --guest-processes for read-only process names/parents (no arguments),
or --sample-guest-process PID for a one-second native sample. Both reuse the same
agent owner and can inspect a stalled provisioning child without cancelling it.
Use DIRECTORY --physical-memory ADDRESS WORDS to inspect at most 64 read-only
guest RAM words through the app's existing control owner, even before boot.
Use --kernel-memory ADDRESS WORDS only for a symbol-identified x86 guest kernel
mapping (at least 0xffffff8000000000). It never reads guest user address spaces.
Use DIRECTORY --display-mode space|retina|scaled for the real menu's live mode
operation and verified guest geometry; --display-state reports its checkmarks.
Use DIRECTORY --display-fingerprint on|off for bounded distinct-presentation
metadata through the real view; disable it after the diagnostic workload.
Use DIRECTORY --fullscreen on|off for the real AppKit transition (not a guest mode claim).
Use DIRECTORY --insert-disc ABSOLUTE_PATH [SLOT] or --eject-disc [SLOT] for the production image operation (slots0..3).
Use DIRECTORY --shutdown to invoke the real app shutdown action (save work first).
Use DIRECTORY --resume-setup to retry the saved legacy transaction through the
production coordinator, with its existing Keychain credential (no password argument).
Use DIRECTORY --force-stop for the real force-stop action, only with the app's
explicit --development-no-power-confirmation launch argument (unsaved work is lost).
Use DIRECTORY --usb-devices to list selectable host USB devices.
Use DIRECTORY --usb-storage-access LOCATION_ID for the normal scoped storage-access request only (no unmount/capture).
Use DIRECTORY --menu-state library|guest|about to focus that real window and inspect File/Actions/Devices.
Use DIRECTORY --usb-connect LOCATION_ID [--unmount] for explicit normal unmount/capture.
Use DIRECTORY --usb-disconnect LOCATION_ID to release it. No force-unmount automation.
Use DIRECTORY --open-settings sharing|microphone to open a guest preference pane.
Use DIRECTORY --stage-probe to transfer DIRECTORY/Transfer.bin using the
production private staging path (a new private guest directory).
Use DIRECTORY --input-json JSON_ARRAY to exercise the production guest input
handler in the visible app, without host UI automation or another QMP client.
Use DIRECTORY --pointer-workload JSON_OBJECT for the existing finite real-window
host-input/presented-pixel diagnostic; calibrate its marker before using timing.
Use DIRECTORY --key-workload JSON_OBJECT for finite app-owned desktop shortcuts
with 50-ms USB key holds, not slow file-polling press/release requests.
Workload "typing" types 24 x characters per cycle at ten keys/second through
the same input path; first focus a throwaway document, never a real document.
Use DIRECTORY --display-buffer-count 2|3 for an explicit same-boot research-only
Metal swap-queue experiment; normal frame ownership and VSync stay unchanged.
Use DIRECTORY --display-acquire-async on|off for research-only drawable acquisition
away from the input thread; immutable source selection stays main-thread owned.
Use DIRECTORY --trace-sound on|off for control-request-only sound tracing.
Use DIRECTORY --trace-sound-mix on|off for a short mixer-write diagnostic.
Use DIRECTORY --trace-audio-gaps on|off for exceptional SDL/HDA buffer gaps only.
Use DIRECTORY --trace-usb on|off for 15-second metadata-only xHCI tracing.
This diagnostic temporarily owns QEMU's log mask; don't combine with audio tracing.
Launch the app with --trace-usb-storage for up to 256 BOT/UAS metadata events
plus up to 64 BOT USB failures per attachment; no file data is logged.
Use DIRECTORY --network-link on|off for an explicit virtual cable experiment.
Use DIRECTORY --network-details for the production connection addresses and menu.
Use DIRECTORY --configure Network|Sound|"File Sharing"|"Remote Login" to show/capture its real settings.
Use DIRECTORY --inspect cpu|pci|devices|memory-map|media|network|interrupts|phy|link-history|registers|serial for a read-only emulator report.
Serial inspects configuration registers only, never UART data or status/FIFOs.
Use DIRECTORY --opengl-pirq-f-route status|BYTE for an explicit research-only
PIRQ F PIC-disable-bit experiment. Record the original byte and restore that
EXACT byte afterward. A changed low routing field is refused by the device.
--record-network-history on the app enables bounded in-memory NIC boot history.
Never connect another client to the app's guest-agent socket.
Commands require a running VM. The 120-second CLI observation window does not
cancel the guest process: inspect the same Running.json/Result.json after an
observation timeout, and never repeat a write just because that window expired.
For private stdin, create DIRECTORY/Input.bin as a user-owned0600 regular file,
then use DIRECTORY --private-stdin /absolute/guest/program [arguments ...]. The
app consumes/removes that bounded file before launching; passwords never enter
the command arguments, request JSON or printed output. The guest program must
also avoid echoing its input.
"""
import json
import os
from pathlib import Path
import sys
import time

def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    directory = Path(sys.argv[1]).resolve()
    request, running, result = (directory / name for name in
                                ("Request.json", "Running.json", "Result.json"))
    if request.exists() or running.exists():
        raise SystemExit("A diagnostic request is already pending; inspect it before retrying.")
    private_input=len(sys.argv)>3 and sys.argv[2]=='--private-stdin'
    if private_input:
        del sys.argv[2]
    command = ({"Action": "installer-log"} if sys.argv[2] == "--installer-log" else
               {"Action": "guest-processes"} if sys.argv[2] == "--guest-processes" else
               {"Action": "guest-process-sample", "PID": int(sys.argv[3])} if sys.argv[2] == "--sample-guest-process" and len(sys.argv) == 4 else
               {"Action": "inspect-hardware", "Component": sys.argv[2][2:], "Address": int(sys.argv[3],0), "Words": int(sys.argv[4],0)} if sys.argv[2] in ("--physical-memory", "--kernel-memory") and len(sys.argv) == 5 else
               {"Action": "menu-state", "Context": sys.argv[3]} if sys.argv[2] == "--menu-state" and len(sys.argv) == 4 and sys.argv[3] in ("library", "guest", "about") else
               {"Action": "opengl-pirq-f-pic", **({} if sys.argv[3] == "status" else {"Route": int(sys.argv[3], 0)})} if sys.argv[2] == "--opengl-pirq-f-route" and len(sys.argv) == 4 else
               {"Action": "usb-storage-access", "LocationID": int(sys.argv[3], 0)} if sys.argv[2] == "--usb-storage-access" and len(sys.argv) == 4 else
               {"Action": "usb-devices"} if sys.argv[2] == "--usb-devices" else
               {"Action": "network-details"} if sys.argv[2] == "--network-details" else
               {"Action": "configuration", "Pane": sys.argv[3]} if sys.argv[2] == "--configure" and len(sys.argv) == 4 else
               {"Action": "usb-connect", "LocationID": int(sys.argv[3], 0), "Unmount": "--unmount" in sys.argv[4:]} if sys.argv[2] == "--usb-connect" else
               {"Action": "usb-disconnect", "LocationID": int(sys.argv[3], 0)} if sys.argv[2] == "--usb-disconnect" else
               {"Action": "clipboard", **({"Enabled": sys.argv[3] == "on"} if len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else {})} if sys.argv[2] == "--clipboard" else
               {"Action": "input", "Events": json.loads(sys.argv[3])} if sys.argv[2] == "--input-json" else
               {**json.loads(sys.argv[3]), "Action": "pointer-workload"} if sys.argv[2] == "--pointer-workload" and len(sys.argv) == 4 else
               {**json.loads(sys.argv[3]), "Action": "key-workload"} if sys.argv[2] == "--key-workload" and len(sys.argv) == 4 else
               {"Action": "display-buffer-count", "Count": int(sys.argv[3])} if sys.argv[2] == "--display-buffer-count" and len(sys.argv) == 4 and sys.argv[3] in ("2", "3") else
               {"Action": "display-acquire-async", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--display-acquire-async" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "trace-sound", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--trace-sound" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "trace-sound-mix", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--trace-sound-mix" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "trace-audio-gaps", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--trace-audio-gaps" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "trace-usb", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--trace-usb" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "network-link", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--network-link" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "inspect-hardware", "Component": sys.argv[3]} if sys.argv[2] == "--inspect" and len(sys.argv) == 4 and sys.argv[3] in ("cpu", "pci", "devices", "memory-map", "media", "network", "interrupts", "phy", "link-history", "registers", "serial") else
               {"Action": "stage-probe"} if sys.argv[2] == "--stage-probe" else
               {"Action": "capture"} if sys.argv[2] == "--capture" else
               {"Action": "cursor-state"} if sys.argv[2] == "--cursor-state" else
               {"Action": "display-state"} if sys.argv[2] == "--display-state" else
               {"Action": "display-fingerprint", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--display-fingerprint" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "fullscreen", "Enabled": sys.argv[3] == "on"} if sys.argv[2] == "--fullscreen" and len(sys.argv) == 4 and sys.argv[3] in ("on", "off") else
               {"Action": "display-mode", "Mode": sys.argv[3]} if sys.argv[2] == "--display-mode" and len(sys.argv) == 4 and sys.argv[3] in ("space", "retina", "scaled") else
               {"Action": "insert-disc", "Path": sys.argv[3], "Slot": int(sys.argv[4]) if len(sys.argv) == 5 else 0} if sys.argv[2] == "--insert-disc" and len(sys.argv) in (4,5) else
               {"Action": "insert-disc", "Path": "", "Slot": int(sys.argv[3]) if len(sys.argv) == 4 else 0} if sys.argv[2] == "--eject-disc" else
               {"Action": "status-probe"} if sys.argv[2] == "--status" else
               {"Action": "shutdown-vm"} if sys.argv[2] == "--shutdown" else
               {"Action": "resume-setup"} if sys.argv[2] == "--resume-setup" else
               {"Action": "force-stop-vm"} if sys.argv[2] == "--force-stop" else
               {"Action": "open-guest-preferences", "Pane": sys.argv[3]} if sys.argv[2] == "--open-settings" and len(sys.argv) == 4 and sys.argv[3] in ("sharing", "microphone") else
               {"Executable": sys.argv[2], "Arguments": sys.argv[3:]})
    if private_input:
        if 'Executable' not in command:
            raise SystemExit('Private stdin requires a guest executable, not an app action.')
        command['PrivateInput']='Input.bin'
    result.unlink(missing_ok=True)
    temporary = directory / f"Request-{os.getpid()}.json"
    with temporary.open("x") as file:
        json.dump(command, file)
    temporary.replace(request)
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        if result.exists():
            response = json.loads(result.read_text())
            print(json.dumps(response, indent=2))
            return int(bool(response.get("Error")) or response.get("ExitCode", 0))
        time.sleep(0.25)
    raise SystemExit("No result yet. The command may still run; do not repeat a write.")

if __name__ == "__main__":
    sys.exit(main())
