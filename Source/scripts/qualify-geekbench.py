#!/usr/bin/env python3
"""Run the private short CPU fixture; never submit results or create a VM.

Uses the visible app's existing diagnostic/guest-agent owner. A timed-out
observation is not retried and does not terminate the benchmark in the guest.
This is developer qualification, not a shipped installation step.
"""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--host', action='store_true')
    target.add_argument('--diagnostics', type=Path)
    parser.add_argument('--guest', help='guest family, e.g. tiger or bigsur')
    parser.add_argument('--backend', help='actual guest CPU backend for the record')
    parser.add_argument('--guest-executable', type=Path,
                        help='benchmark already copied to a private guest test folder, e.g. through the SMB acceptance transfer')
    parser.add_argument('--output', required=True, type=Path, help='new result directory')
    args = parser.parse_args()
    if args.diagnostics and (not args.guest or not args.backend):
        parser.error('Guest runs require --guest and --backend.')
    if args.guest_executable and (args.host or not args.guest_executable.is_absolute()):
        parser.error('--guest-executable requires guest diagnostics and an absolute guest path.')
    source = Path(__file__).resolve().parents[1]
    name = 'geekbench' if args.guest in ('tiger', 'leopard') else 'geekbench-64'
    binary = source / 'vendor/geekbench-2006' / name
    if not binary.is_file():
        parser.error('The private benchmark fixture is not available in this checkout.')
    args.output.mkdir(parents=True, exist_ok=False)
    xml = args.output / 'geekbench.xml'
    observations = []

    def request(*command):
        run = subprocess.run([sys.executable, str(source / 'scripts/app-guest-command.py'),
                              str(args.diagnostics), *command], capture_output=True, text=True)
        try:
            result = json.loads(run.stdout)
        except ValueError:
            raise RuntimeError(run.stderr or run.stdout or 'No result; do not replay a running command.')
        observations.append(result)
        (args.output / 'commands.json').write_text(json.dumps(observations, indent=2))
        if run.returncode or result.get('Error') or result.get('ExitCode', 0):
            raise RuntimeError(result.get('Error') or result.get('Stderr') or 'Guest benchmark operation failed.')
        return result

    if args.host:
        command = (["/usr/bin/arch", "-x86_64"] if platform.machine() == 'arm64' else [])
        command += [str(binary), '-n', '-o', str(xml.resolve())]
        with (args.output / 'console.txt').open('w') as console:
            subprocess.run(command, stdout=console, stderr=subprocess.STDOUT, check=True)
    else:
        if args.guest_executable:
            executable = str(args.guest_executable)
            result_file = str(args.guest_executable.with_name('geekbench.xml'))
        else:
            shutil.copyfile(binary, args.diagnostics / 'Transfer.bin')
            staged = request('--stage-probe')['Transfer']['Directory']
            executable = staged + '/Transfer.bin'
            result_file = staged + '/geekbench.xml'
        request('/bin/chmod', '755', executable)
        request(executable, '-n', '-o', result_file)
        xml.write_text(request('/bin/cat', result_file)['Stdout'])
    root = ET.parse(xml).getroot()
    metrics = {item.attrib['name']: item.attrib['value'] for item in root.findall('./metrics/metric')}
    summary = {'Guest': args.guest if not args.host else 'host',
               'Backend': args.backend if not args.host else ('Rosetta' if platform.machine() == 'arm64' else 'native'),
               'Executable': name, 'Score': float(root.find('score').attrib['value']),
               'ElapsedSeconds': float(root.find('elapsed').attrib['value']), 'Metrics': metrics}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
