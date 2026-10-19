#!/usr/bin/env python3
"""Read-only inventory of host image containers using the packaged block driver.

This does not qualify a guest mount. It never mounts, converts or executes the
contents of an image. JSON output separates unsupported containers from readable
ones; use the visible app's --insert-disc diagnostics for real guest qualification.
"""
import argparse
import json
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('qemu_img',type=Path)
parser.add_argument('directory',type=Path)
args=parser.parse_args()
results=[]
for path in sorted(args.directory.iterdir()):
    if not path.is_file() or path.suffix.lower() not in ('.dmg','.iso','.cdr'):
        continue
    kind='dmg' if path.suffix.lower()=='.dmg' else 'raw'
    try:
        run=subprocess.run([str(args.qemu_img),'info','--output=json','-f',kind,'--',str(path)],
                           capture_output=True,text=True,timeout=15)
        item={'Name':path.name,'Format':kind,'Readable':run.returncode==0}
        if not run.returncode:
            info=json.loads(run.stdout)
            item['VirtualSize']=info.get('virtual-size')
        else:
            item['Reason']=run.stderr.strip()[:2048]
    except (OSError,subprocess.TimeoutExpired,ValueError) as error:
        item={'Name':path.name,'Readable':False,'Reason':str(error)}
    results.append(item)
print(json.dumps(results,indent=2))
