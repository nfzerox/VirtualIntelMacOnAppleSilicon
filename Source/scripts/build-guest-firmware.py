#!/usr/bin/env python3
"""Build the x64 guest's quiet, S3-aware OVMF using vendored pinned EDK II.

Optional maintainer operation, not part of UI iteration. No administrator access
or host-system changes. All source edits/build output go into a private copy;
no Git dependency is fetched or changed. The same x64 firmware is
used on Intel/HVF and Apple silicon/TCG. Normal app builds use qualified prebuilts.
"""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

from firmware_source import checkout_receipt, extract
import build_freshness as freshness

SOURCE = Path(__file__).resolve().parents[1]
TOOL_INPUTS = {
    'llvm-mingw-20261006-ucrt-macos-universal.tar.xz': (
        'https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/',
        'a7a19b11048e89372637d26279cdb7c430ed3cf8c1e224310421312bf986ebf6'),
    'nasm-3.02.tar.xz': ('https://www.nasm.us/pub/nasm/releasebuilds/3.02/',
        '87336eba53b4acfe917424ab5d500d2b0054d9f5148d35c2273ccf2cfb712f0d'),
    'acpica-unix2-20260408.tar.gz': (
        'https://github.com/acpica/acpica/releases/download/20260408/',
        '5fcef9f0d00ffaffaaa82aa49c5ea9687986e8a7bd60929e298cc99a6741ae24'),
}


def run(*args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def digest(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def replace_once(root, relative, original, replacement):
    """Fail closed on changed source; preserve the upstream file's line endings."""
    path = root / relative
    data = path.read_bytes()
    newline = b'\r\n' if b'\r\n' in data else b'\n'
    old = original.encode().replace(b'\n', newline)
    new = replacement.encode().replace(b'\n', newline)
    if data.count(new) == 1:
        return  # incremental build of this exact patched copy
    if data.count(old) != 1:
        raise RuntimeError('Firmware source no longer matches: ' + relative)
    path.write_bytes(data.replace(old, new, 1))


def patch_sources(root):
    # Apple's /usr/bin/make is GNU Make 3.81, which predates grouped targets
    # (&:). VfrCompile's one parser/lexer generation recipe is otherwise run
    # concurrently for multiple outputs, truncating files during compilation.
    # Serialize only this small tool; the other BaseTools and firmware modules
    # still use the requested parallelism. No generated parser is vendored.
    replace_once(root, 'BaseTools/Source/C/VfrCompile/GNUmakefile',
        'APPNAME = VfrCompile', '.NOTPARALLEL:\n\nAPPNAME = VfrCompile')
    # Stock macOS ships Bash 3.2, which has no negative array subscripts.
    replace_once(root, 'BaseTools/BinWrappers/PosixLike/GenericShellWrapper',
        '${BASH_SOURCE[-1]:-$0}', '${BASH_SOURCE[${#BASH_SOURCE[@]}-1]:-$0}')
    # LLVM 23 diagnoses the upstream x64 memcpy alias's 32-bit count. Its real
    # implementation and the C ABI use UINTN/size_t; do not hide that mismatch
    # by disabling the diagnostic.
    replace_once(root, 'CryptoPkg/Library/IntrinsicLib/CopyMem.c',
        '  unsigned int  count', '  size_t        count')
    # OVMF deliberately omits MemTypeInfoInitialization on S3. Initializing bins
    # anyway asserts on its absent HOB. All allocation paths already guard on
    # MemoryTypeInformationInitialized and use the PHIT when it is false.
    replace_once(root, 'MdeModulePkg/Core/Pei/Memory/MemoryServices.c',
        '      PrivateData->MemoryTypeInformation = AllocateZeroPool',
        '      // OVMF S3 wake has no memory-type-information HOB. Retain the\n'
        '      // existing PHIT allocator rather than attempting cold-boot bins.\n'
        '      if (((EFI_HOB_HANDOFF_INFO_TABLE *)HobList)->BootMode == BOOT_ON_S3_RESUME) {\n'
        '        return;\n'
        '      }\n\n'
        '      PrivateData->MemoryTypeInformation = AllocateZeroPool')
    replace_once(root, 'OvmfPkg/Library/PlatformBootManagerLib/BdsPlatform.c',
        '  BootLogoEnableLogo ();',
        '  // Virtual Intel Mac remains black until the guest draws its Apple logo.')
    replace_once(root, 'OvmfPkg/Library/PlatformBootManagerLib/BdsPlatform.c',
        '  PlatformBmPrintScRegisterHandler ();',
        '  // Keep firmware boot-status text off the guest display. Serial diagnostics remain.')
    for relative in ('OvmfPkg/OvmfPkgX64.dsc', 'OvmfPkg/OvmfPkgX64.fdf'):
        replace_once(root, relative, '  MdeModulePkg/Logo/LogoDxe.inf' if relative.endswith('dsc')
                     else 'INF MdeModulePkg/Logo/LogoDxe.inf',
                     '  # Virtual Intel Mac: no firmware branding image.')


def prepare_tools(tools, jobs, bootstrap):
    llvm = tools / 'llvm-mingw-20261006-ucrt-macos-universal'
    sources = tools / 'Sources'
    nasm = sources / 'nasm-3.02/nasm'
    acpi = sources / 'acpica-unix2-20260408/generate/unix'
    iasl = acpi / 'bin/iasl'
    if bootstrap:
        downloads = tools / 'Downloads'
        downloads.mkdir(parents=True, exist_ok=True)
        sources.mkdir(parents=True, exist_ok=True)
        for name, (base, expected) in TOOL_INPUTS.items():
            archive = downloads / name
            if not archive.exists():
                partial = downloads / (name + '.partial')
                run('curl', '-fL', '--retry', '2', '--max-time', '180', base+name, '-o', partial)
                if digest(partial) != expected:
                    raise RuntimeError('Downloaded tool checksum mismatch: ' + name)
                partial.rename(archive)
            if digest(archive) != expected:
                raise RuntimeError('Cached tool checksum mismatch: ' + name)
            destination = tools if name.startswith('llvm-') else sources
            directory = name.removesuffix('.tar.xz').removesuffix('.tar.gz')
            if not (destination / directory).is_dir():
                run('/usr/bin/tar', '-xf', archive, '-C', destination)
        if not nasm.is_file():
            if not (nasm.parent / 'Makefile').exists():
                run('./configure', cwd=nasm.parent)
            run('/usr/bin/make', f'-j{jobs}', 'nasm', cwd=nasm.parent)
        if not iasl.is_file():
            # The upstream parallel parser rules require this directory first.
            (acpi / 'iasl/obj').mkdir(exist_ok=True)
            compiler = '/usr/bin/xcrun --sdk macosx clang'
            # ACPICA's packed ACPI_EXDUMP_INFO tables contain unaligned pointer
            # fields. ARM64 chained fixups cannot encode them; classic dyld
            # relocations can. This affects the native build tool, not firmware.
            run('/usr/bin/make', f'-j{jobs}', 'CC='+compiler,
                'LDFLAGS=-Wl,-no_fixup_chains', cwd=acpi/'iasl')
    required = [nasm, iasl, llvm/'bin/clang', llvm/'bin/lld-link', llvm/'bin/llvm-lib']
    if not all(path.is_file() for path in required):
        raise RuntimeError('Missing private firmware tools. Run once with --bootstrap-tools.')
    return llvm, nasm, iasl


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu', type=Path, help='legacy option; firmware source is now vendored independently')
    p.add_argument('--edk-source', type=Path, help='explicit maintainer override: complete pristine pinned checkout, never fetched')
    p.add_argument('--source-only', action='store_true', help='verify/extract and patch vendored source offline, without building tools or firmware')
    p.add_argument('--verbose-diagnostics', action='store_true', help='serial debug-port INFO output; never draws guest text')
    p.add_argument('--work', type=Path, default=SOURCE/'Build/GuestFirmware')
    p.add_argument('--tools', type=Path, default=SOURCE/'Build/FirmwareTools')
    p.add_argument('--bootstrap-tools', action='store_true', help='fetch checksum-pinned standard build tools privately')
    p.add_argument('--configuration', choices=['DEBUG', 'RELEASE'], default='RELEASE')
    p.add_argument('--jobs', type=int, default=4)
    a = p.parse_args()
    build_inputs = freshness.snapshot(SOURCE, 'firmware')
    if a.jobs < 1 or a.jobs > 64:
        p.error('--jobs must be between 1 and 64')
    if a.qemu:
        print('--qemu is a legacy option; using the independently pinned firmware source.', flush=True)
    work = a.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    lock = (work/'Build.lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise RuntimeError('A firmware build still owns this work directory. Wait for it to finish; do not overlap builds.')
    root = work / 'edk2'
    if a.edk_source:
        edk = a.edk_source.resolve()
        source_receipt = checkout_receipt(edk)
        stamp = root/'.VIMCheckoutReceipt.plist'
        if root.exists():
            if not stamp.is_file() or plistlib.loads(stamp.read_bytes()) != source_receipt:
                raise RuntimeError('Unrecognized firmware work tree; use a new --work directory.')
        else:
            run('/usr/bin/rsync', '-a', '--exclude=.git', '--exclude=Build', str(edk)+'/', str(root)+'/')
            stamp.write_bytes(plistlib.dumps(source_receipt))
    else:
        source_receipt = extract(SOURCE, root)
    revision = source_receipt['Revision']
    patch_sources(root)
    if a.source_only:
        print('Verified/patched offline firmware source:', root, flush=True)
        return
    llvm, nasm, iasl = prepare_tools(a.tools.resolve(), a.jobs, a.bootstrap_tools)
    env = os.environ.copy()
    env.update(PYTHON_COMMAND=sys.executable, PYTHONHASHSEED='1', WORKSPACE=str(root),
               CLANG_BIN=str(llvm/'bin')+'/', NASM_PREFIX=str(nasm.parent)+'/',
               IASL_PREFIX=str(iasl.parent)+'/',
               PATH=str(Path(sys.executable).parent)+':'+env.get('PATH', ''))
    # BaseTools run natively and use the host SDK, not the Windows cross compiler.
    run('/usr/bin/make', '-C', root/'BaseTools/Source/C', f'-j{a.jobs}',
        'CC=/usr/bin/xcrun --sdk macosx clang',
        'CXX=/usr/bin/xcrun --sdk macosx clang++', env=env)
    flags = ['-a', 'X64', '-b', a.configuration, '-t', 'CLANGPDB', '-n', str(a.jobs),
             '-p', 'OvmfPkg/OvmfPkgX64.dsc', '-D', 'FD_SIZE_4MB=TRUE',
             '-D', 'BUILD_SHELL=FALSE', '-D', 'NETWORK_ENABLE=FALSE',
             '-D', 'TPM2_ENABLE=FALSE', '-D', 'TPM1_ENABLE=FALSE',
             '-D', 'CC_MEASUREMENT_ENABLE=FALSE',
             '--pcd', 'gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel='+('0x8000004f' if a.verbose_diagnostics else '0x80000000'),
             '--pcd', 'gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut=0']
    # edksetup is the upstream environment initializer, not a guest/host patch.
    run('/bin/bash', '-c', 'firmware_args=("$@"); set --; . ./edksetup.sh && build "${firmware_args[@]}"', 'build-guest-firmware', *flags, cwd=root, env=env)
    output = work / ('Output-'+a.configuration)
    output.mkdir(exist_ok=True)
    fv = root / ('Build/OvmfX64/'+a.configuration+'_CLANGPDB/FV')
    hashes = {}
    for name, size in [('OVMF_CODE.fd', 3653632), ('OVMF_VARS.fd', 540672)]:
        source = fv / name
        if source.stat().st_size != size:
            raise RuntimeError('Firmware geometry changed; existing guest NVRAM must not be reused blindly.')
        shutil.copy2(source, output/name)
        hashes[name] = digest(output/name)
    (output/'BuildReceipt.plist').write_bytes(plistlib.dumps({
        'Freshness': freshness.seal(build_inputs, freshness.snapshot(SOURCE, 'firmware'), output,
                                    [output/name for name in hashes]),
        'EDKRevision': revision, 'Configuration': a.configuration, 'GuestArchitecture': 'x86_64',
        'QuietBoot': True, 'S3MemoryBinGuard': True, 'VerboseDiagnostics': a.verbose_diagnostics, 'Hashes': hashes,
        'Tools': {name: checksum for name, (_, checksum) in TOOL_INPUTS.items()},
        'SourceReceipt': source_receipt,
    }))
    print('Built firmware candidate (runtime qualification required):', output, flush=True)


if __name__ == '__main__':
    main()
