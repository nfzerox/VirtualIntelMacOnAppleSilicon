#!/usr/bin/env python3
"""Finite transition input through the real app's virtual USB keyboard.

No guest CGEventPost, AX/TCC permission, recorder, observer or second QMP client.
Queued replies prove dispatch, not guest delivery, visible transitions or FPS;
qualify with actual compositor probes or a separate host-window recording.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from opengl_transition_evidence import summarize, visible_frame


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('diagnostics', type=Path)
    parser.add_argument('--workload', choices=['all', 'mission-control', 'app-expose', 'spaces'], default='all')
    parser.add_argument('--cycles', type=int, choices=range(1, 5), default=2)
    parser.add_argument('--measure', action='store_true', help='Bounded real app presentation journal; no video/FPS claim')
    parser.add_argument('--output', type=Path, required=True, help='NEW dispatch-only .json journal')
    args = parser.parse_args()
    if args.output.exists() or args.output.suffix != '.json':
        parser.error('Choose a new .json output')
    adapter = Path(__file__).with_name('app-guest-command.py')
    began = time.monotonic()
    rows = []

    def key(code, down):
        return {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': code}}}

    def invoke(label, *arguments):
        start = time.monotonic()
        result = subprocess.run([sys.executable, str(adapter), str(args.diagnostics),
                                 *arguments], capture_output=True, text=True)
        end = time.monotonic()
        rows.append({'Action': label, 'Start': start - began, 'Returned': end - began,
                     'HostStartMonotonic': start, 'HostReturnMonotonic': end,
                     'ExitCode': result.returncode, 'Reply': result.stdout, 'Error': result.stderr})
        if result.returncode:
            raise RuntimeError('Dispatch failed; inspect the existing request before retrying')
        return json.loads(result.stdout)

    def send(label, events):
        if not invoke(label, '--input-json', json.dumps(events)).get('Queued'):
            raise RuntimeError('The app did not acknowledge input dispatch')

    failure = None
    before = after = None
    armed = False
    try:
        invoke('focus-real-guest', '--menu-state', 'guest')
        send('initial-release', [key('ctrl', False), key('meta_l', False), key('esc', False)])
        if args.measure:
            before = invoke('measurement-on', '--display-fingerprint', 'on')
            armed = True
            if not visible_frame(before['Frame']):
                raise RuntimeError('The guest window is hidden, asleep or has no display.')
        workloads = ['mission-control', 'app-expose', 'spaces'] if args.workload == 'all' else [args.workload]
        for workload in workloads:
            for cycle in range(args.cycles):
                for arrow in (['right', 'left'] if workload == 'spaces' else
                              ['up'] if workload == 'mission-control' else ['down']):
                    send('%s-%d-press' % (workload, cycle), [key('ctrl', True), key(arrow, True)])
                    send('release', [key(arrow, False), key('ctrl', False)])
                    time.sleep(.8)
                    if workload != 'spaces':
                        send('escape-down', [key('esc', True)])
                        send('escape-up', [key('esc', False)])
                        time.sleep(.8)
    except Exception as error:
        failure = str(error)
    finally:
        if not any((args.diagnostics/name).exists() for name in ('Request.json', 'Running.json')):
            try:
                send('cleanup-release', [key(code, False) for code in ('ctrl', 'meta_l', 'esc', 'up', 'down', 'left', 'right')])
            except Exception as error:
                failure = failure or str(error)
        if armed and not any((args.diagnostics/name).exists() for name in ('Request.json', 'Running.json')):
            try:
                after = invoke('measurement-off', '--display-fingerprint', 'off')
                armed = False
            except Exception as error:
                failure = failure or str(error)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        evidence = {'DispatchOnly': not args.measure, 'Workload': args.workload,
            'Cycles': args.cycles, 'Actions': rows, 'Failure': failure,
            'MeasurementBefore': before, 'MeasurementAfter': after, 'MeasurementMayRemainArmed': armed,
            'Elapsed': time.monotonic()-began}
        if args.measure:
            evidence['MeasurementSummary'] = summarize(evidence)
        args.output.write_text(json.dumps(evidence, indent=2)+'\n')
    if failure:
        raise SystemExit(failure)
    if args.measure and not evidence['MeasurementSummary']['MeasurementValid']:
        raise SystemExit('; '.join(evidence['MeasurementSummary']['Issues']))
    print(args.output)


if __name__ == '__main__':
    main()
