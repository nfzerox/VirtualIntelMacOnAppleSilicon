#!/usr/bin/env python3
"""Export real installer icon slices and an optional compact OS wallpaper."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--guest',required=True,choices=('lion','mountainlion','mavericks','yosemite','elcapitan','sierra','highsierra'))
p.add_argument('--installer',type=Path,required=True)
p.add_argument('--wallpaper',type=Path)
a=p.parse_args()
catalog=Path(__file__).resolve().parents[1]/'VirtualIntelMac/Assets.xcassets'
resources=a.installer/'Contents/Resources'
for suffix,original in (('Icon','InstallAssistant.icns'),('Roundel','ProductPageIcon.icns')):
    with tempfile.TemporaryDirectory(prefix='vim-os-art-') as stage:
        icons=Path(stage)/'Art.iconset'
        subprocess.run(['iconutil','-c','iconset','-o',str(icons),str(resources/original)],check=True)
        image=next((icons/name for name in ('icon_128x128@2x.png','icon_256x256.png') if (icons/name).is_file()),None)
        if image is None:raise ValueError('The original installer has no 256-pixel icon slice')
        output=catalog/(a.guest+suffix+'.imageset');output.mkdir(exist_ok=True)
        shutil.copyfile(image,output/'icon.png')
        (output/'Contents.json').write_text(json.dumps({'images':[{'filename':'icon.png','idiom':'universal'}],
            'info':{'author':'xcode','version':1}},indent=2)+'\n')
if a.wallpaper:
    output=catalog/(a.guest+'Wallpaper.imageset');output.mkdir(exist_ok=True)
    subprocess.run(['sips','-s','format','jpeg','-s','formatOptions','82','-Z','1280',
        str(a.wallpaper),'--out',str(output/'wallpaper.jpg')],check=True)
    (output/'Contents.json').write_text(json.dumps({'images':[{'filename':'wallpaper.jpg','idiom':'universal'}],
        'info':{'author':'xcode','version':1}},indent=2)+'\n')
