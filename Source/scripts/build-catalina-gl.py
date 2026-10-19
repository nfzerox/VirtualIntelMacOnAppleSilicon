#!/usr/bin/env python3
"""Build the Metal GL driver addition, leaving Catalina's OpenGL unchanged."""
import argparse
import gzip
import io
from pathlib import Path
import plistlib
import runpy
import shutil
import subprocess
import tarfile
import tempfile


def build(output):
    source = Path(__file__).resolve().parents[1]
    if output.exists():
        raise ValueError('Choose a new output directory')
    output.mkdir(parents=True)
    extract = runpy.run_path(str(source/'scripts/package-app-runtime.py'))['extract_host_inputs']
    with tempfile.TemporaryDirectory(prefix='vim-catalina-gl-') as temporary:
        work = Path(temporary)
        extract(source, work, 'mojave-gl-inputs.tar.gz', 'SHA256SUMS')
        inputs = work/'gl-inputs'
        subprocess.run(['/bin/zsh', str(source/'scripts/build-opengl-donor-probe'), str(work/'driver')],
                       env={**__import__('os').environ, 'PVG_TARGET_OS': '10.15',
                            'PVG_GL_DONOR': str(inputs/'AppleMetalOpenGLRenderer.bundle')}, check=True)
        payload = output/'payload'
        extensions = payload/'System/Library/Extensions'
        extensions.mkdir(parents=True)
        # The original Catalina Metal-only release predates the shared GL AIR
        # normalization fix. Rebuild that small bridge too, preserving the
        # rest of its signed plugin bundle and the native Catalina compiler.
        metal_prefix = 'payload/System/Library/Extensions/AppleParavirtGPUMetal.bundle'
        seed = runpy.run_path(str(source/'scripts/payload-archive.py'))['release_archive'](source/'prebuilt')
        seed_hash = [line.split()[0] for line in (source/'prebuilt/SHA256SUMS').read_text().splitlines()
                     if len(line.split()) == 2 and line.split()[1] == seed.name]
        import hashlib
        if seed_hash != [hashlib.sha256(seed.read_bytes()).hexdigest()]:
            raise ValueError('Catalina Metal seed checksum mismatch')
        serializer_prefix = 'payload/System/Library/PrivateFrameworks/MetalSerializer.framework'
        with tarfile.open(seed, 'r:*') as archive:
            for member in archive:
                if any(member.name == prefix or member.name.startswith(prefix+'/')
                       for prefix in (metal_prefix, serializer_prefix)):
                    archive.extract(member, output, filter='data')
        metal = extensions/'AppleParavirtGPUMetal.bundle'
        bridge = metal/'Contents/Resources/libAppleParavirtCompilerPlugin.dylib'
        subprocess.run(['xcrun', 'clang', '-O2', '-arch', 'x86_64', '-mmacosx-version-min=10.15',
                        '-DPVG_GL_FIX_FETCH_METADATA=1', '-dynamiclib',
                        str(source/'compat/PVGCompilerPluginBridge/PVGCompilerPluginBridge.c'),
                        str(source/'support/GPUCompiler3902LLVM.tbd'),
                        '-install_name', '/System/Library/Extensions/AppleParavirtGPUMetal.bundle/Contents/Resources/libAppleParavirtCompilerPlugin.dylib',
                        '-o', str(bridge)], check=True)
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', str(bridge)], check=True)
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', str(metal)], check=True)
        serializer = payload/'System/Library/PrivateFrameworks/MetalSerializer.framework'
        sdk = source/'resources/xcode/XcodeCatalina.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
        subprocess.run(['xcrun', 'clang', '-O2', '-arch', 'x86_64', '-mmacosx-version-min=10.15',
                        '-fblocks', '-isysroot', str(sdk), '-dynamiclib',
                        str(source/'compat/PVGMetalCompat/PVGMetalCompat.m'),
                        str(source/'compat/PVGMetalCompat/PVGWritability.m'),
                        '-undefined', 'dynamic_lookup', '-install_name',
                        '/System/Library/PrivateFrameworks/MetalSerializer.framework/Versions/A/MetalSerializer',
                        '-Wl,-not_for_dyld_shared_cache',
                        '-Wl,-reexport_library,'+str(serializer/'Versions/A/MetalSerializerOriginal'),
                        '-framework', 'Foundation', '-framework', 'Metal', '-lobjc',
                        '-o', str(serializer/'Versions/A/MetalSerializer')], check=True)
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', str(serializer/'Versions/A/MetalSerializer')], check=True)
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', str(serializer)], check=True)
        bundle = extensions/'AppleMetalOpenGLRenderer.bundle'
        shutil.copytree(work/'driver/AppleMetalOpenGLRenderer.bundle', bundle, symlinks=True)
        plugin = bundle/'Contents/PlugIns/libGLDPlugin3902.dylib'
        plugin.parent.mkdir(exist_ok=True)
        shutil.copyfile(inputs/'libGLDPlugin3902.dylib', plugin)
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', str(plugin)], check=True)
        # Lowered AIR 2.1 builtins work on Catalina's Metal 2.2 compiler too.
        # Preserve the actual shader implementation, including MSAA resolve.
        metallib = source/'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/metallib'
        builtins = bundle/'Contents/Resources/default.metallib'
        builtins.unlink()
        subprocess.run([str(metallib), *map(str, sorted((inputs/'air21').glob('*.air'))),
                        '-o', str(builtins)], check=True)
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', str(bundle)], check=True)
        # Catalina MTLCompiler searches the older spelling. Keep this tiny
        # alias in the same transaction; no replacement OpenGL frontend.
        (extensions/'AppleMetalGLRenderer.bundle').symlink_to('AppleMetalOpenGLRenderer.bundle')
        subprocess.run(['/bin/zsh', str(source/'scripts/build-opengl-probe'), str(work/'registration')], check=True)
        registration = payload/'Library/Extensions/PVGOpenGLProbe.kext'
        registration.parent.mkdir(parents=True)
        shutil.copytree(work/'registration/PVGOpenGLProbe.kext', registration)
        for path in (bundle, registration, metal, serializer):
            subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(path)], check=True)
    archive = output/'catalina-gl.tar.gz'
    with archive.open('xb') as file, gzip.GzipFile(fileobj=file, mode='wb', filename='', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.USTAR_FORMAT) as stream:
            for path in [payload, *sorted(payload.rglob('*'))]:
                member = stream.gettarinfo(str(path), path.relative_to(output).as_posix())
                member.uid = member.gid = member.mtime = 0
                member.uname = member.gname = ''
                member.pax_headers = {}
                if member.isfile():
                    with path.open('rb') as data:
                        stream.addfile(member, data)
                else:
                    stream.addfile(member)
    import hashlib
    (output/'SHA256SUMS').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  catalina-gl.tar.gz\n')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    print(build(parser.parse_args().output))
