#!/usr/bin/env python3
"""Export supplied Apple installer icon slices and compact library wallpapers.
Run only when updating artwork; generated image sets are checked into the repo.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('assets', type=Path)
p.add_argument('--product-icons-only',action='store_true',help='update supplied OS identity artwork without touching wallpapers/installer roundels')
p.add_argument('--bigsur-wallpaper', type=Path,
               help='Big Sur Graphic.heic from the donor Desktop Pictures folder')
p.add_argument('--bigsur-dark', type=Path, help='Big Sur Graphic.heic: export second image')
p.add_argument('--catalina-dark', type=Path, help='Catalina.heic: export second image')
p.add_argument('--mojave-dark', type=Path, help='Mojave Night.heic: export sixteenth image')
args = p.parse_args()
catalog = Path(__file__).resolve().parents[1]/'VirtualIntelMac/Assets.xcassets'
source = catalog.parent.parent
for guest,stem in [('mojave','Mojave'),('catalina','Catalina'),('bigsur','BigSur')]:
    folder=args.assets/'product-icon'
    imageset=catalog/(guest+'ProductIcon.imageset');imageset.mkdir(exist_ok=True)
    images=[]
    for variant,name in [('light',stem+'ProductPageIcon.icns'),('dark',stem+'DarkProductPageIcon.icns')]:
        original=folder/name
        if not original.is_file():
            if variant=='light':raise RuntimeError('Missing product artwork: '+str(original))
            continue
        with tempfile.TemporaryDirectory(prefix='vim-product-art-') as temp:
            icons=Path(temp)/'art.iconset'
            subprocess.run(['/usr/bin/iconutil','-c','iconset','-o',str(icons),str(original)],check=True)
            icon=next((icons/name for name in ('icon_128x128@2x.png','icon_256x256.png') if (icons/name).is_file()),None)
            if not icon:raise RuntimeError('No actual 256-pixel product icon slice: '+str(original))
            filename='art.png' if variant=='light' else 'art-dark.png'
            shutil.copyfile(icon,imageset/filename)
            image={'filename':filename,'idiom':'universal'}
            if variant=='dark':image['appearances']=[{'appearance':'luminosity','value':'dark'}]
            images.append(image)
    (imageset/'Contents.json').write_text(json.dumps({'images':images,'info':{'author':'xcode','version':1}},indent=2)+'\n')
if args.product_icons_only:raise SystemExit(0)
exporter = source/'Build/export-app-artwork'
if any((args.bigsur_dark, args.catalina_dark, args.mojave_dark)):
    exporter.parent.mkdir(exist_ok=True)
    subprocess.run(['xcrun','clang','-fobjc-arc','-fmodules',str(source/'tools/export-app-artwork.m'),
                    '-framework','Cocoa','-framework','ImageIO','-o',str(exporter)],check=True)
for guest, stem, wallpaper in [('mojave','Mojave','Mojave.heic'),
                              ('catalina','Catalina','Catalina.heic'),
                              ('bigsur','BigSur','Big Sur Graphic.heic')]:
    with tempfile.TemporaryDirectory(prefix='vim-art-') as temp:
        icons = Path(temp)/(guest+'.iconset')
        subprocess.run(['/usr/bin/iconutil','-c','iconset','-o',str(icons),str(args.assets/(stem+'_InstallAssistant.icns'))],check=True)
        # Use an actual 256-pixel slice, not the ICNS's unspecified default rep.
        icon = next((icons/name for name in ('icon_128x128@2x.png','icon_256x256.png') if (icons/name).is_file()), None)
        if not icon:
            raise RuntimeError(f'{guest} has no 256-pixel icon slice')
        for suffix, filename in [('Icon','art.png'),('Wallpaper','art.jpg')]:
            imageset=catalog/(guest+suffix+'.imageset')
            imageset.mkdir(exist_ok=True)
            output=imageset/filename
            if suffix=='Icon':
                shutil.copyfile(icon,output)
            else:
                subprocess.run(['/usr/bin/sips','-s','format','jpeg','-s','formatOptions','82','-Z','1280',
                    str(args.bigsur_wallpaper if guest=='bigsur' and args.bigsur_wallpaper else args.assets/wallpaper),
                    '--out',str(output)],check=True)
            images = [{'filename':filename,'idiom':'universal'}]
            if suffix == 'Wallpaper':
                dark = getattr(args, guest+'_dark')
                if dark:
                    subprocess.run([str(exporter),'wallpaper',str(dark),'15' if guest=='mojave' else '1',
                                    str(imageset/'art-dark.jpg')],check=True)
                if (imageset/'art-dark.jpg').is_file():
                    images.append({'filename':'art-dark.jpg','idiom':'universal',
                                   'appearances':[{'appearance':'luminosity','value':'dark'}]})
            (imageset/'Contents.json').write_text(json.dumps({'images':images,
                'info':{'author':'xcode','version':1}},indent=2)+'\n')
