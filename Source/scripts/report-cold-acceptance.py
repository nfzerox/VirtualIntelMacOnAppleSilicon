#!/usr/bin/env python3
"""Summarize an app acceptance run from its existing host receipts and capture.

Run on the test controller, not on a stock test Mac without developer tools.
Read-only: it neither stops a VM nor deletes a disk or installer. The caller
still records how the target was created and any intervention along the way.
"""
import argparse
import json
from pathlib import Path
import plistlib
import struct


def read(path):
    try:
        with path.open('rb') as file:
            value = plistlib.load(file)
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError, plistlib.InvalidFileException):
        return {}


def dimensions(path):
    if path is None:
        return None
    try:
        with path.open('rb') as file:
            header = file.read(24)
            file.seek(-12, 2)
            end = file.read(12)
    except OSError:
        return None
    if (len(header) != 24 or header[:16] != b'\x89PNG\r\n\x1a\n\0\0\0\rIHDR' or
            end != b'\0\0\0\0IEND\xaeB`\x82'):
        return None
    width, height = struct.unpack('>II', header[16:24])
    return [width, height] if width and height else None


def inspect(machine, capture):
    config = read(machine / 'Machine.plist')
    state = machine / 'State'
    guest = config.get('Guest')
    legacy = read(state / 'LegacyGuestInstallation.plist')
    modern = read(state / 'GuestInstallation.plist')
    install = read(state / 'Install.plist')
    profile = read(state / 'AutomaticSetup.plist')
    if guest in {'mojave', 'catalina', 'bigsur'}:
        verification = modern.get('PostBootVerification', {})
        report = read(state / 'GuestStatus.plist').get('Report', {})
        graphics = (modern.get('Phase') == 'graphics-verified' and
                    verification.get('MetalCommandVerified') is True and
                    verification.get('VerifiedAfterReboot') is True and
                    verification.get('BootSessionUUID') and
                    verification['BootSessionUUID'] == report.get('BootSessionUUID'))
        account = read(state / 'NativeSetup.plist').get('Phase')
        recovered = None
        audio = None  # Modern graphics receipts do not attest to audio.
    else:
        report = legacy.get('GuestReport', {})
        graphics = (legacy.get('Phase') == 'graphics-verified' and
                    legacy.get('MachineIdentifier') == config.get('Identifier') and
                    report.get('PixelChecksPassed') is True)
        account = report.get('Account', {}).get('Phase')
        recovered = report.get('Account', {}).get('LoginHandoffRecovered') is True
        audio = report.get('AudioInitialized')
    setup = profile.get('Enabled') is True
    gates = {
        'desktop': report.get('DesktopRunning') in (True, 1) and report.get('ConsoleUID', 0) >= 501,
        'graphics': bool(graphics),
        'account': not setup or account == 'complete',
        'installed': config.get('InstallState') == 'installed' and not config.get('InstallerMedia'),
        'installerCleanup': install.get('Phase') == 'complete' and install.get('InstallerDetached') is True,
        'captureSize': dimensions(capture),
    }
    return {'guest': guest, 'guestBuild': report.get('ProductBuildVersion'), 'gates': gates,
            'automaticSetup': setup, 'automaticLoginRecovered': recovered, 'audioInitialized': audio,
            'desktopCheckpoint': all(gates[k] for k in ('desktop', 'graphics', 'account', 'installed', 'captureSize')),
            'note': 'This records milestones, not uninterrupted cold-install success or visual correctness.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('machine', type=Path)
    parser.add_argument('--capture', type=Path)
    args = parser.parse_args()
    result = inspect(args.machine, args.capture)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['desktopCheckpoint'] else 1)
