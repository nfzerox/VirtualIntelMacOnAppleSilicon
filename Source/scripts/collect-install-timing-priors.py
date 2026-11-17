#!/usr/bin/env python3
"""Produce one optional, identity-free timing baseline plus global scales.

Pass CPU/filesystem profiles followed by their Timing.plist snapshots. Multiple
observations of the same guest use the last supplied successful run. This is
offline developer tooling; neither the app nor installer needs Python.
Example:
  collect-install-timing-priors.py --profile arm64-exfat ARM27-*.plist \
      --profile arm64-apfs ARM15-*.plist --profile x86_64-apfs Intel26-*.plist
ARM/internal APFS supplies the per-guest baseline. User-selected global factors
are2x for Intel and3x for exFAT, independent of internal/external storage.
Outputs a plist to stdout; paths, OS/host names, dates and user data are omitted.
"""
import argparse
from pathlib import Path
import plistlib
import sys

SHIPPING = frozenset(('tiger', 'leopard', 'snowleopard', 'lion', 'mountainlion',
                     'mavericks', 'yosemite', 'elcapitan', 'sierra', 'highsierra',
                     'mojave', 'catalina', 'bigsur'))


def collect(groups):
    profiles = {}
    for profile, *paths in groups:
        guests = profiles.setdefault(profile, {})
        for name in paths:
            timing = plistlib.loads(Path(name).read_bytes())
            if not timing.get('Succeeded') or timing.get('Guest') not in SHIPPING:
                continue
            stages = [{'Stage': s['Stage'], 'Seconds': round(float(s['Seconds']), 2)}
                      for s in timing['Stages'] if s['Seconds'] > 0]
            if stages:
                guests[timing['Guest']] = stages
    return {'Guests': profiles.get('arm64-apfs', {}),
            'Scales': {'Intel': 2.0, 'ExFAT': 3.0}}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', nargs='+', action='append', required=True,
                        metavar='PROFILE_OR_TIMING', help='CPU-filesystem key, then timing files')
    args = parser.parse_args()
    sys.stdout.buffer.write(plistlib.dumps(collect(args.profile), sort_keys=True))
