#!/usr/bin/env python3
"""Prepare app payloads, omitting reference images regenerated in each guest.

Build-host tooling only. Archives are never extracted or installed on the host.
Existing release inputs remain untouched. Big Sur native/GL setup is separate.
"""
import argparse
import lzma
import hashlib
import io
from pathlib import Path, PurePosixPath
import plistlib
import tarfile
import runpy
import subprocess
import tempfile

release_archive = runpy.run_path(str(Path(__file__).with_name('payload-archive.py')))['release_archive']

REGENERATED = (
    'System/Library/Frameworks/CoreDisplay.framework',
    'System/Library/PrivateFrameworks/SkyLight.framework',
    'System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/3903',
    'System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc',
)

def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            value.update(block)
    return value.hexdigest()

def mojave_renderer(source, archive):
    """Patch and seal the complete small renderer bundle, retaining its resources."""
    patch = runpy.run_path(str(source/'scripts/patch-gl-argb-fallback.py'))['candidate']
    with tempfile.TemporaryDirectory(prefix='vim-gl-argb-') as temporary:
        prefix = 'payload/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle/'
        bundle = Path(temporary)/'AppleMetalOpenGLRenderer.bundle'
        with tarfile.open(archive, 'r:*') as original:
            for member in original:
                if not member.name.startswith(prefix):
                    continue
                relative = PurePosixPath(member.name[len(prefix):])
                if relative.is_absolute() or '..' in relative.parts or not (member.isdir() or member.isfile()):
                    raise ValueError('Unexpected renderer bundle member')
                path = bundle/str(relative)
                if member.isdir():
                    path.mkdir(parents=True, exist_ok=True)
                else:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(original.extractfile(member).read())
                    path.chmod(member.mode & 0o777)
        image = bundle/'Contents/MacOS/AppleMetalOpenGLRenderer'
        data = image.read_bytes()
        symbols = subprocess.check_output(['/usr/bin/nm', '-n', str(image)], text=True)
        image.write_bytes(patch(data, symbols))
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', str(bundle)], check=True)
        subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
        return {prefix + str(p.relative_to(bundle)): p.read_bytes()
                for p in (image, bundle/'Contents/_CodeSignature/CodeResources')}

def build_bigsur(source, output, helpers=None):
    archive=output/'bigsur.tar.xz'
    helpers=helpers or source/'prebuilt/bigsur'
    with archive.open('xb') as file:
        with lzma.LZMAFile(file, 'w', preset=6, check=lzma.CHECK_CRC64) as compressed:
            with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.USTAR_FORMAT) as result:
                for name in ('payload','payload/GuestHelpers'):
                    member=tarfile.TarInfo(name);member.type=tarfile.DIRTYPE;member.mode=0o755
                    result.addfile(member)
                for name in ('PVGBigSurMetalCompat.dylib','PVGOpenGLMetal.dylib'):
                    helper=helpers/name
                    if not helper.is_file():raise ValueError('Missing Big Sur guest adapter: '+name)
                    member=tarfile.TarInfo('payload/GuestHelpers/'+name)
                    member.size=helper.stat().st_size;member.mode=0o755
                    with helper.open('rb') as data:result.addfile(member,data)
    return {'Archive':archive.name,'SHA256':digest(archive),'EntryCount':4,
        'RequiresGuestStaging':True,'NativeGPUBundlesFromGuest':True,
        'SystemVolumeChanges':False,'BootCacheRebuildRequired':False}

def build(source, output, surface_helper=None, bigsur_helpers=None, iosurface_root=None):
    output.mkdir(parents=True, exist_ok=False)
    receipt = {'FormatVersion': 1, 'RegeneratedFromGuest': list(REGENERATED), 'Guests': {}}
    for guest, directory in (('mojave', source/'prebuilt/mojave'), ('catalina', source/'prebuilt')):
        # Small, independently rebuilt project driver. Do not inherit an older
        # copy embedded in the large historical Apple donor archive.
        driver_root = iosurface_root or source/'prebuilt/iosurface'
        driver = driver_root/guest/'PVGIOSurfaceABI.kext'
        if not (driver/'Contents/MacOS/PVGIOSurfaceABI').is_file():
            raise ValueError('Missing rebuilt IOSurface ABI driver: '+guest)
        driver_files = {str(p.relative_to(driver_root)): digest(p)
                        for p in sorted(driver.rglob('*')) if p.is_file()}
        manifest = (driver_root/'SHA256SUMS').read_text().splitlines()
        trusted = dict((line.split('  ',1)[1],line.split('  ',1)[0]) for line in manifest)
        if not driver_files or any(trusted.get(p) != h for p,h in driver_files.items()):
            raise ValueError('IOSurface ABI driver checksum mismatch: '+guest)
        subprocess.run(['/usr/bin/codesign','--verify','--deep','--strict',str(driver)],check=True)
        driver_prefix = 'Library/Extensions/PVGIOSurfaceABI.kext'
        archive = release_archive(directory)
        expected = [line.split()[0] for line in (directory/'SHA256SUMS').read_text().splitlines()
                    if len(line.split()) == 2 and line.split()[1].lstrip('*') == archive.name]
        actual = digest(archive)
        if expected != [actual]:
            raise ValueError(f'{guest}: release archive checksum mismatch')
        destination = output/(guest+'.tar.xz')
        renderer_overrides = mojave_renderer(source, archive) if guest == 'mojave' else {}
        kept, omitted = [], []
        with tarfile.open(archive, 'r:*') as original, destination.open('xb') as file:
            with lzma.LZMAFile(file, 'w', preset=6, check=lzma.CHECK_CRC64) as compressed:
                with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.USTAR_FORMAT) as result:
                    for member in original:
                        path = PurePosixPath(member.name)
                        if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != 'payload':
                            raise ValueError(f'Unsafe release member: {member.name}')
                        if not (member.isfile() or member.isdir() or member.issym()):
                            raise ValueError(f'Unsupported release member: {member.name}')
                        relative = '/'.join(path.parts[1:])
                        if relative == driver_prefix or relative.startswith(driver_prefix+'/'):
                            omitted.append(relative)
                            continue
                        if guest == 'catalina' and any(relative == p or relative.startswith(p+'/') for p in
                            ('System/Library/Extensions/AppleParavirtGPUMetal.bundle',
                             'System/Library/PrivateFrameworks/MetalSerializer.framework')):
                            omitted.append(relative)
                            continue
                        if any(relative == prefix or relative.startswith(prefix+'/') for prefix in REGENERATED):
                            omitted.append(relative)
                            continue
                        if member.issym():
                            # Resolve lexically inside the archive; never follow host paths.
                            target = PurePosixPath(member.linkname)
                            if target.is_absolute():
                                raise ValueError(f'Absolute release symlink: {member.name}')
                            stack = list(path.parts[:-1])
                            for part in target.parts:
                                if part == '..':
                                    if len(stack) <= 1: raise ValueError('Release symlink escapes payload')
                                    stack.pop()
                                elif part != '.': stack.append(part)
                        member.uid = member.gid = 0
                        member.uname = member.gname = ''
                        member.mtime = 0
                        member.pax_headers = {}  # No extracted guest metadata in product archives.
                        data = original.extractfile(member) if member.isfile() else None
                        if member.name in renderer_overrides:
                            if not member.isfile():
                                raise ValueError('Mojave renderer is not a regular file')
                            patched = renderer_overrides[member.name]
                            member.size = len(patched)
                            data = io.BytesIO(patched)
                        result.addfile(member, data)
                        kept.append(relative)
                    for path in [driver, *sorted(driver.rglob('*'))]:
                        relative = driver_prefix
                        if path != driver:
                            relative += '/' + path.relative_to(driver).as_posix()
                        if relative in kept or path.is_symlink() or not (path.is_file() or path.is_dir()):
                            raise ValueError('Unsupported rebuilt IOSurface ABI member')
                        member = result.gettarinfo(str(path), 'payload/'+relative)
                        member.uid = member.gid = member.mtime = 0
                        member.uname = member.gname = ''
                        member.pax_headers = {}
                        if path.is_file():
                            with path.open('rb') as data: result.addfile(member,data)
                        else: result.addfile(member)
                        kept.append(relative)
                    if guest == 'catalina':
                        addition = source/'prebuilt/catalina-gl/catalina-gl.tar.gz'
                        checks = source/'prebuilt/catalina-gl/SHA256SUMS'
                        expected_gl = [line.split()[0] for line in checks.read_text().splitlines()
                                       if len(line.split()) == 2 and line.split()[1] == addition.name]
                        if expected_gl != [digest(addition)]:
                            raise ValueError('Catalina GL addition checksum mismatch')
                        allowed = ('Library/Extensions/PVGOpenGLProbe.kext',
                                   'System/Library/Extensions/AppleMetalOpenGLRenderer.bundle',
                                   'System/Library/Extensions/AppleMetalGLRenderer.bundle',
                                   'System/Library/Extensions/AppleParavirtGPUMetal.bundle',
                                   'System/Library/PrivateFrameworks/MetalSerializer.framework')
                        with tarfile.open(addition, 'r:gz') as extra:
                            for member in extra:
                                path = PurePosixPath(member.name)
                                if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != 'payload':
                                    raise ValueError('Unsafe Catalina GL archive path')
                                relative = '/'.join(path.parts[1:])
                                included = any(relative == p or relative.startswith(p+'/') for p in allowed)
                                if not included:
                                    if member.isdir() and any(p.startswith(relative+'/') or not relative for p in allowed):
                                        continue
                                    raise ValueError('Unexpected Catalina GL target: '+relative)
                                if relative in kept or not (member.isfile() or member.isdir() or member.issym()):
                                    raise ValueError('Duplicate/unsupported Catalina GL member')
                                if member.issym():
                                    target = PurePosixPath(member.linkname)
                                    if target.is_absolute():
                                        raise ValueError('Absolute Catalina GL symlink')
                                    resolved = list(path.parts[:-1])
                                    for part in target.parts:
                                        if part == '..':
                                            if len(resolved) <= 1:
                                                raise ValueError('Catalina GL symlink escapes payload')
                                            resolved.pop()
                                        elif part != '.':
                                            resolved.append(part)
                                member.uid = member.gid = member.mtime = 0
                                member.uname = member.gname = ''
                                member.pax_headers = {}
                                result.addfile(member, extra.extractfile(member) if member.isfile() else None)
                                kept.append(relative)
                    # Fresh QEMU guests do not inherit the HDA driver that was
                    # installed manually in the original development VMs.
                    audio = source/'prebuilt/audio/VoodooHDA.kext'
                    if not (audio/'Contents/MacOS/VoodooHDA').is_file():
                        raise ValueError('Missing prebuilt guest HDA driver')
                    for path in [audio, *sorted(audio.rglob('*'))]:
                        relative = 'Library/Extensions/VoodooHDA.kext'
                        if path != audio:
                            relative += '/' + path.relative_to(audio).as_posix()
                        if relative in kept:
                            raise ValueError('Duplicate audio driver in release payload')
                        member = result.gettarinfo(str(path), 'payload/'+relative)
                        member.uid = member.gid = member.mtime = 0
                        member.uname = member.gname = ''
                        member.pax_headers = {}
                        if path.is_file():
                            with path.open('rb') as data:
                                result.addfile(member, data)
                        else:
                            result.addfile(member)
                        kept.append(relative)
                    if guest == 'mojave':
                        # SkyLight itself is regenerated from this guest, but
                        # its Surface2 compatibility dependency is our code.
                        helper = surface_helper or source/'prebuilt/mojave/PVGGLIOKit.dylib'
                        if not helper.is_file():
                            raise ValueError('Missing Mojave Surface2 helper')
                        relative = 'usr/lib/PVGGLIOKit.dylib'
                        member = result.gettarinfo(str(helper), 'payload/'+relative)
                        member.uid = member.gid = member.mtime = 0
                        member.uname = member.gname = ''
                        member.pax_headers = {}
                        with helper.open('rb') as data:
                            result.addfile(member, data)
                        kept.append(relative)
        if not omitted:
            raise ValueError(f'{guest}: no reference files removed; inspect release format')
        if guest == 'mojave':
            required = {'System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL',
                        'usr/lib/PVGGLIOKit.dylib',
                        'System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/PVGDonorFiles.txt'}
            if not required.issubset(kept):
                raise ValueError('Mojave payload is missing the Catalina OpenGL donor or its staging manifest')
        receipt['Guests'][guest] = {'Archive': destination.name, 'SHA256': digest(destination),
                                   'SourceArchiveSHA256': actual, 'EntryCount': len(kept),
                                   'RemovedReferenceEntries': omitted,
                                   'IOSurfaceABIFiles': driver_files,
                                   'RequiresGuestStaging': True}
    # Big Sur keeps its own Metal/OpenGL/compiler stack. Only our two small
    # adapters ship; native GPU bundles are prepared from the actual guest.
    receipt['Guests']['bigsur']=build_bigsur(source,output,bigsur_helpers)
    (output/'Payloads.plist').write_bytes(plistlib.dumps(receipt, sort_keys=True))
    return receipt

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    receipt = build(Path(__file__).resolve().parents[1], args.output)
    for guest, record in receipt['Guests'].items():
        print(f'{guest}: {record["EntryCount"]} retained entries; {len(record.get("RemovedReferenceEntries", []))} guest-reference entries omitted')
