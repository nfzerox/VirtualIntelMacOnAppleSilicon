#!/usr/bin/env python3
"""Create separate development-only signature-bypass EFI; never edit a VM/app."""
import argparse
from pathlib import Path
import runpy
import tempfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--guest',choices=('mojave','catalina','bigsur'),default='mojave')
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    if args.output.exists():parser.error('Choose a new output; existing firmware is never replaced.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    source=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='.amfi-efi-',dir=args.output.parent) as stage:
        directory=Path(stage)
        packager=runpy.run_path(str(source/'scripts/package-app-runtime.py'))
        packager['extract_host_inputs'](
            source,directory,'app-firmware-inputs.tar.gz','app-firmware-SHA256SUMS')
        registration=packager['registration_kext'](source,directory/'PVGOpenGLProbe.kext') if args.guest=='bigsur' else None
        runpy.run_path(str(source/'scripts/make-efi-disk.py'))['create'](
            directory/args.guest/'BOOT.img',args.output,quiet=True,registration=registration,signature_only_amfi=True)
    print('Development probe only. Launch one recoverable guest with --guest-efi '+str(args.output.resolve()))

if __name__=='__main__':main()
