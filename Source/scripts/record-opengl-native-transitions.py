#!/usr/bin/env python3
"""Finite real-guest transitions, measured through the visible app's sole owner.

Use the already installed native CGEvent workload and authenticated SSH master;
no AX, substitute test window, new QMP connection, credentials or preferences.
This removes slow per-key app request polling from transition timing. Posted
input still requires separate visible-scene confirmation. Counts include idle;
the journal is NOT an FPS/conformance or physical input-latency acceptance.
"""
import argparse
import json
import posixpath
import shlex
from pathlib import Path
import subprocess
import sys
import time
from opengl_transition_evidence import summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('diagnostics', type=Path)
    parser.add_argument('--ssh-control', type=Path, required=True)
    parser.add_argument('--workload', choices=('spaces', 'app-expose', 'mission-control', 'launchpad'), required=True)
    parser.add_argument('--native-input', default='/var/tmp/opengl-native-transition-input',
                        help='Already installed absolute guest path; Launchpad requires the Launch Services-capable helper')
    parser.add_argument('--doorbell', choices=('on', 'off'), required=True,
                        help='Explicit negotiated same-boot research control, not a guest preference')
    parser.add_argument('--output', type=Path, required=True, help='NEW .json journal')
    args = parser.parse_args()
    if not posixpath.isabs(args.native_input) or '\n' in args.native_input or '\r' in args.native_input:
        parser.error('Use an absolute guest input-helper path')
    if not args.diagnostics.is_dir() or not args.ssh_control.is_socket():
        parser.error('Use the live app diagnostics directory and authenticated SSH master socket')
    if args.output.exists() or args.output.suffix != '.json' or not args.output.parent.is_dir():
        parser.error('Choose a new .json file in an existing evidence directory')
    adapter = Path(__file__).with_name('app-guest-command.py')
    ssh = ['ssh', '-n', '-S', str(args.ssh_control), '-p', '2222', '-o', 'BatchMode=yes', 'mac@127.0.0.1']
    before = after = None
    actions = []
    armed = False
    failure = None
    began = time.monotonic()

    def run(label, command, json_reply=False):
        start = time.monotonic()
        process = subprocess.run(command, capture_output=True, text=True)
        end = time.monotonic()
        actions.append({'Action': label, 'StartSeconds': start-began, 'ReturnSeconds': end-began,
                        'HostStartMonotonic': start, 'HostReturnMonotonic': end,
                        'ExitCode': process.returncode, 'Reply': process.stdout, 'Error': process.stderr})
        if process.returncode:
            raise RuntimeError('A control/workload failed; inspect the saved journal before retrying')
        return json.loads(process.stdout) if json_reply else process.stdout

    def measure(mode):
        return run('measurement-'+mode, [sys.executable, str(adapter), str(args.diagnostics),
                                        '--display-fingerprint', mode], True)

    def visible(snapshot):
        frame = snapshot['Frame']
        return (frame.get('WindowOcclusionVisible') == 1 and
                not any(frame.get(key) for key in ('Sleeping', 'GuestDisplaySleeping', 'PresentationHidden')) and
                frame.get('PixelWidth', 0) > 0 and frame.get('PixelHeight', 0) > 0)

    try:
        control = run('doorbell-'+args.doorbell,
                      ssh+['/var/tmp/opengl-framebuffer-blit-control-doorbell doorbell-'+args.doorbell])
        wanted = 'doorbell_supported=1 doorbell_enabled='+str(int(args.doorbell == 'on'))
        if wanted not in control:
            raise RuntimeError('The owning provider did not confirm the negotiated doorbell selection')
        run('focus-real-guest', [sys.executable, str(adapter), str(args.diagnostics),
                                 '--menu-state', 'guest'], True)
        run('initial-escape', ssh+[shlex.join([args.native_input, 'escape'])])
        before = measure('on')
        armed = True
        if not visible(before):
            raise RuntimeError('The real guest window is hidden, asleep or fully occluded; no workload sent')
        run('native-'+args.workload, ssh+[shlex.join([args.native_input, args.workload])])
    except Exception as error:
        failure = str(error)
    finally:
        if armed:
            # No competing app request: never hide an observation failure
            # by opening another QMP/QGA client or replaying unknown input.
            if any((args.diagnostics/name).exists() for name in ('Request.json', 'Running.json')):
                failure = failure or 'An app request is pending; measurement teardown is unconfirmed'
            else:
                try:
                    after = measure('off')
                    armed = False
                    if not visible(after):
                        failure = failure or 'Guest presentation became hidden, asleep or fully occluded'
                    if any(before['Frame'][key] != after['Frame'][key] for key in ('PixelWidth', 'PixelHeight', 'BackingScale')):
                        failure = failure or 'Display geometry/density changed during the workload'
                except Exception as error:
                    failure = failure or str(error)
        journal = {'Workload': args.workload, 'Doorbell': args.doorbell, 'NativeInput': args.native_input,
                   'HostBeganMonotonic': began, 'Actions': actions, 'Failure': failure,
                   'MeasurementBefore': before, 'MeasurementAfter': after,
                   'MeasurementMayRemainArmed': armed, 'InputDeliveryRequiresVisualConfirmation': True,
                   'Caution': 'No input-to-photon/FPS claim; native helper posts real events but has no delivery ACK.'}
        journal['MeasurementSummary'] = summarize(journal)
        if journal['MeasurementSummary']['MeasurementValid'] and not journal['MeasurementSummary']['RunCounterDeltas']['DistinctFrames']:
            journal['Failure'] = 'No changed image was presented; input delivery/effect is unqualified'
            journal['MeasurementSummary'] = summarize(journal)
        args.output.write_text(json.dumps(journal, indent=2)+'\n')
    print(args.output)
    print(json.dumps(journal['MeasurementSummary'], sort_keys=True))
    if failure or not journal['MeasurementSummary']['MeasurementValid']:
        raise SystemExit(failure or 'Invalid measurement; inspect the journal')


if __name__ == '__main__':
    main()
