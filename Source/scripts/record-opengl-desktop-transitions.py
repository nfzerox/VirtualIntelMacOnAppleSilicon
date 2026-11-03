#!/usr/bin/env python3
"""Record real VM transitions via the visible app's sole control owner.

Bounded host-window video is visual correctness evidence, NOT an FPS or
input-latency benchmark. No per-frame guest capture, second QMP connection,
guest AX permission or special test window. Prepare two or more real Spaces
before the spaces workload. An optional installed native CGEvent helper uses
an already authenticated SSH master, avoiding file-polling between key edges.
Neither path opens another QMP/QGA connection. Launchpad without that helper
uses SSH to open the system app and app-owned input for dismissal.
Use --metadata-only with a NEW .json output to omit screen capture/encoding
when measuring presentation metadata. Keep separate movies for visual review:
capture overhead must not silently become the guest's measured pacing cost.
Notification Center takes an explicit measured icon location in QEMU's
normalized 0..32767 coordinates. The window ID comes from CGWindowList.
"""
import argparse
import json
from pathlib import Path
import posixpath
import shlex
import subprocess
import sys
import time
from legacy_transition_evidence import summarize


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('diagnostics', type=Path)
    p.add_argument('--window-id', type=int, required=True)
    p.add_argument('--workload', choices=['mission-control', 'mission-control-select', 'app-expose', 'spaces',
                                        'launchpad', 'notification-center', 'fullscreen'], required=True)
    p.add_argument('--notification-icon', type=int, nargs=2, metavar=('X', 'Y'),
                   help='Measured Notification Center icon in normalized guest coordinates')
    p.add_argument('--space-thumbnail', type=int, nargs=2, metavar=('X', 'Y'),
                   help='Measured Desktop 2 thumbnail in normalized coordinates; begin on Desktop 1')
    p.add_argument('--guest-ssh-control', type=Path, help='Existing authenticated SSH master for THIS VM')
    p.add_argument('--guest-ssh-port', type=int, default=2222)
    p.add_argument('--native-input', help='Already installed absolute guest CGEvent-helper path; posts two finite native cycles')
    p.add_argument('--metadata-only', action='store_true', help='No screen recorder; require a NEW .json output')
    p.add_argument('--output', type=Path, required=True, help='NEW .mov (video) or .json (--metadata-only) path')
    a = p.parse_args()
    journal = a.output.with_suffix('.json')
    if a.output.exists() or journal.exists() or a.output.suffix != ('.json' if a.metadata_only else '.mov') or a.window_id <= 0:
        p.error('Choose a valid window ID and new .mov (video) or .json (--metadata-only) path')
    if not a.diagnostics.is_dir():
        p.error('The actual app diagnostics directory is required')
    if a.workload == 'notification-center' and (not a.notification_icon or
            any(value < 0 or value > 32767 for value in a.notification_icon)):
        p.error('Notification Center needs its measured --notification-icon X Y')
    if a.workload == 'mission-control-select' and (not a.space_thumbnail or
            any(value < 0 or value > 32767 for value in a.space_thumbnail)):
        p.error('Desktop selection needs its measured --space-thumbnail X Y; begin on Desktop 1')
    if (a.workload == 'launchpad' or a.native_input) and (not a.guest_ssh_control or not a.guest_ssh_control.is_socket()):
        p.error('This workload needs an existing authenticated --guest-ssh-control socket for this VM')
    if a.native_input and (not posixpath.isabs(a.native_input) or '\n' in a.native_input or '\r' in a.native_input or
            a.workload not in ('mission-control', 'app-expose', 'spaces', 'fullscreen', 'launchpad')):
        p.error('Use an absolute native helper path with a supported native workload')
    if not 1 <= a.guest_ssh_port <= 65535:
        p.error('Choose a valid guest SSH port')
    adapter = Path(__file__).with_name('app-guest-command.py')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    before = after = None
    armed = False
    button_may_be_held = False
    modifiers_may_be_held = False
    began = time.monotonic()
    recorder = None if a.metadata_only else subprocess.Popen(['screencapture', '-x', '-v', '-V', '26',
                                                             '-l' + str(a.window_id), str(a.output)])

    def event(code, down):
        return {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': code}}}

    def run(label, command, json_reply=True):
        start = time.monotonic() - began
        result = subprocess.run(command, capture_output=True, text=True)
        rows.append({'Action': label, 'StartSeconds': start,
                     'ReturnSeconds': time.monotonic() - began, 'ExitCode': result.returncode,
                     'Reply': result.stdout, 'Error': result.stderr})
        if result.returncode:
            raise RuntimeError('The control command failed; inspect the companion journal before retrying')
        return json.loads(result.stdout) if json_reply else result.stdout

    def invoke(label, *arguments):
        return run(label, [sys.executable, str(adapter), str(a.diagnostics), *arguments])

    def keys(label, events):
        if recorder is not None and recorder.poll() is not None:
            raise RuntimeError('The host recorder exited before the workload finished')
        return invoke(label, '--input-json', json.dumps(events))

    def click(label, position=None):
        nonlocal button_may_be_held
        x, y = position if position is not None else a.notification_icon
        # Separate down/up transactions: one USB poll can coalesce both
        # edges in a single QMP batch into no actual guest click.
        button_may_be_held = True
        keys(label+'-down', [{'type': 'abs', 'data': {'axis': 'x', 'value': x}},
                            {'type': 'abs', 'data': {'axis': 'y', 'value': y}},
                            {'type': 'btn', 'data': {'button': 'left', 'down': True}}])
        keys(label+'-up', [{'type': 'btn', 'data': {'button': 'left', 'down': False}}])
        button_may_be_held = False

    failure = None
    try:
        # Recording preroll only, not an assumed guest-operation deadline.
        time.sleep(1)
        keys('initial-release', [event('ctrl', False), event('meta_l', False), event('esc', False)])
        if a.native_input:
            run('native-initial-escape',
                ['ssh', '-n', '-S', str(a.guest_ssh_control), '-p', str(a.guest_ssh_port),
                 '-o', 'BatchMode=yes', 'mac@127.0.0.1', shlex.join([a.native_input, 'escape'])], False)
        else:
            # Separate edges: one HID report must not collapse a press and
            # release into no guest Escape. This path is visual evidence only.
            keys('initial-escape-down', [event('esc', True)])
            time.sleep(.05)
            keys('initial-escape-up', [event('esc', False)])
        time.sleep(.5) # Recording preroll, not an assertion that the desktop is ready.
        before = invoke('measurement-on', '--display-fingerprint', 'on')
        armed = True
        if a.native_input:
            run('native-'+a.workload,
                ['ssh', '-n', '-S', str(a.guest_ssh_control), '-p', str(a.guest_ssh_port),
                 '-o', 'BatchMode=yes', 'mac@127.0.0.1', shlex.join([a.native_input, a.workload])], False)
        for cycle in range(0 if a.native_input else 2):
            if a.workload == 'mission-control-select':
                # Actual thumbnail selection, not Escape back to the same
                # desktop. Preserve the whole bar/shadow settling interval.
                # Explicit coordinates avoid guessing private Dock layouts.
                for label, arrow in [('enter', 'up'), ('return', 'left')]:
                    modifiers_may_be_held = True
                    keys('cycle-%d-%s-control-down' % (cycle, label), [event('ctrl', True)])
                    keys('cycle-%d-%s-arrow' % (cycle, label), [event(arrow, True), event(arrow, False)])
                    keys('control-up', [event('ctrl', False)])
                    modifiers_may_be_held = False
                    time.sleep(.9)
                    if label == 'enter':
                        click('cycle-%d-select-desktop-2' % cycle, a.space_thumbnail)
                        time.sleep(1.2)
                continue
            if a.workload == 'fullscreen':
                # Guest Finder's standard shortcut, not the host AppKit
                # fullscreen action. Prepare a real eligible guest window.
                for direction in ('enter', 'exit'):
                    modifiers_may_be_held = True
                    keys('cycle-%d-%s-modifiers' % (cycle, direction),
                         [event('ctrl', True), event('meta_l', True)])
                    keys('cycle-%d-%s-fullscreen' % (cycle, direction),
                         [event('f', True), event('f', False)])
                    keys('release-modifiers', [event('meta_l', False), event('ctrl', False)])
                    modifiers_may_be_held = False
                    time.sleep(1.2)
                continue
            if a.workload == 'notification-center':
                click('cycle-%d-open-notifications' % cycle)
                time.sleep(.6)
                click('cycle-%d-close-notifications' % cycle)
                time.sleep(.6)
                continue
            if a.workload == 'launchpad':
                # Constant system command, no credential storage, arbitrary
                # shell text, guest AX setup, second QMP/QGA owner or polling.
                run('cycle-%d-open-launchpad' % cycle,
                    ['ssh', '-n', '-S', str(a.guest_ssh_control), '-p', str(a.guest_ssh_port),
                     '-o', 'BatchMode=yes', 'mac@127.0.0.1', '/usr/bin/open -a Launchpad'], False)
                time.sleep(.6)
                keys('dismiss', [event('esc', True), event('esc', False)])
                time.sleep(.6)
                continue
            for arrow in (['right', 'left'] if a.workload == 'spaces' else
                          ['up'] if a.workload == 'mission-control' else ['down']):
                modifiers_may_be_held = True
                keys('control-down', [event('ctrl', True)])
                keys('cycle-%d-trigger-%s-arrow' % (cycle, arrow), [event(arrow, True), event(arrow, False)])
                keys('control-up', [event('ctrl', False)])
                modifiers_may_be_held = False
                time.sleep(.6)
                if a.workload != 'spaces':
                    keys('dismiss', [event('esc', True), event('esc', False)])
                    time.sleep(.6)
    except Exception as error:
        failure = str(error)
    finally:
        if modifiers_may_be_held and not any((a.diagnostics/name).exists() for name in ('Request.json', 'Running.json')):
            try:
                invoke('cleanup-modifier-release', '--input-json', json.dumps([
                    event('meta_l', False), event('ctrl', False), event('f', False)]))
                modifiers_may_be_held = False
            except Exception as error:
                failure = failure or str(error)
        if button_may_be_held and not any((a.diagnostics/name).exists() for name in ('Request.json', 'Running.json')):
            try:
                invoke('cleanup-button-release', '--input-json', json.dumps([
                    {'type': 'btn', 'data': {'button': 'left', 'down': False}}]))
                button_may_be_held = False
            except Exception as error:
                failure = failure or str(error)
        if armed and not any((a.diagnostics/name).exists() for name in ('Request.json', 'Running.json')):
            try:
                after = invoke('measurement-off', '--display-fingerprint', 'off')
                armed = False
            except Exception as error:
                failure = failure or str(error)
        # A failed/slow observation never justifies a second input owner or
        # retrying a pending command. Leave recovery explicit in the journal.
        evidence = {'Workload': a.workload, 'WindowID': a.window_id, 'NativeInput': a.native_input,
            'RecorderStartedAtMonotonic': began,
            'MetadataOnly': a.metadata_only, 'VideoRecorded': recorder is not None,
            'VisualCorrectnessOnly': True, 'Actions': rows, 'Failure': failure,
            'MeasurementBefore': before, 'MeasurementAfter': after,
            'MeasurementMayRemainArmed': armed,
            'InputButtonMayRemainHeld': button_may_be_held,
            'InputModifiersMayRemainHeld': modifiers_may_be_held,
            'NotificationIcon': a.notification_icon,
            'SpaceThumbnail': a.space_thumbnail,
            'WorkloadEndSeconds': time.monotonic() - began}
        evidence['MeasurementSummary'] = summarize(evidence)
        journal.write_text(json.dumps(evidence, indent=2) + '\n')
        if recorder is not None:
            recorder.wait()
    if failure or (recorder is not None and recorder.returncode):
        raise SystemExit(failure or 'The host recorder failed')
    if recorder is not None:
        print(a.output)
    print(journal)


if __name__ == '__main__':
    main()
