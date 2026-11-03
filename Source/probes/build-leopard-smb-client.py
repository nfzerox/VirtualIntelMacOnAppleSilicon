#!/usr/bin/env python3
"""Build Apple's period-native Darwin8/9 SMB clients without installing them.

No system installation, generated source rewriting, or compatibility stubs.
Use the original Xcode source phase, a genuine period SDK and Apple GCC.
Every failed unit retains its compiler diagnostics. Linking emits the original
native relocatable format; it does not establish runtime ABI compatibility.
The separate packaging script consumes qualified components for app builds.
"""
import argparse
import json
from pathlib import Path
import plistlib
import runpy
import shlex
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "sdk", "cc", "binutils", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--xnu", type=Path,
                        help="Optional matching XNU private headers; no fabricated layouts")
    parser.add_argument("--link",action="store_true",
                        help="Link a native relocatable bundle; never install/load it")
    parser.add_argument("--trace",action="store_true",
                        help="Optional failed SMB transaction diagnostics, never credentials or paths")
    parser.add_argument("--vendor-headers",type=Path,
                        help="Copy only actually used XNU headers and their license to a new durable directory")
    args = parser.parse_args()
    source, sdk, cc, binutils, output = (
        getattr(args, name).resolve() for name in ("source", "sdk", "cc", "binutils", "output"))
    output.mkdir(parents=True, exist_ok=False)
    compiler = subprocess.check_output([str(cc), "--version"], text=True)
    if "(GCC)" not in compiler or "clang" in compiler:
        parser.error("Darwin9 C++ kernel code requires the actual period Apple GCC")
    version=plistlib.loads((sdk/"SDKSettings.plist").read_bytes()).get("Version")
    if version not in ("10.4","10.5"):
        parser.error("Supply the genuine10.4u or10.5 kernel SDK")
    project=source/("smb.pbproj/project.pbxproj" if version=="10.4" else "smb.xcodeproj/project.pbxproj")
    objects = json.loads(subprocess.check_output([
        "plutil", "-convert", "json", "-o", "-", str(project)]))["objects"]
    parents = {child: key for key, item in objects.items() if item.get("isa") == "PBXGroup"
               for child in item.get("children", [])}

    def resolve(key):
        item = objects[key]
        tree = item.get("sourceTree", "<group>")
        if tree == "SOURCE_ROOT":
            return source / item["path"]
        if tree != "<group>":
            raise ValueError("Unsupported source tree " + tree)
        return (resolve(parents[key]) if key in parents else source) / item.get("path", "")

    target = next(item for item in objects.values()
                  if item.get("isa") in ("PBXNativeTarget","PBXBundleTarget") and
                  item.get("name") == ("smbfs" if version=="10.4" else "smbfs.kext"))
    kernel = sdk / "System/Library/Frameworks/Kernel.framework"
    flags = ["-m32", "-B" + str(binutils) + "/", "-isysroot", str(sdk),
             "-mmacosx-version-min="+version, "-nostdinc", "-fno-builtin", "-fno-exceptions",
             "-fno-rtti", "-msoft-float", "-Os", "-fno-common", "-mkernel", "-fapple-kext",
             "-fno-jump-tables", "-DKERNEL", "-DKERNEL_PRIVATE", "-DDRIVER_PRIVATE",
             "-DAPPLE", "-DNeXT", "-D__APPLE_API_UNSTABLE"]
    if version=="10.4":
        # Original Tiger target definitions, not later named-stream/VFS APIs.
        flags += ["-DSMP","-DNETSMBCRYPTO=1","-DNSMB=1"]
    else:
        flags += ["-DNAMEDSTREAMS"]
    if args.trace:
        flags += ["-DVIM_SMB_RESEARCH"]
    for directory in (source / "kernel", source / "kernel/sys5", source / "kernel/mysys", source / "kernel/fs",
                      kernel / "PrivateHeaders", kernel / "Headers"):
        flags += ["-I", str(directory)]
    if version=="10.4":
        # Tiger's smbfs_lockf.c includes its own project's smb_subr.h without
        # a netsmb/ prefix; do not rewrite that original upstream source.
        flags += ["-I",str(source/"kernel/netsmb")]
    if args.xnu:
        xnu = args.xnu.resolve()
        overlay = output / "PrivateHeaders"
        overlay.mkdir()
        (overlay / "crypto").mkdir()
        (overlay / "crypto/des.h").symlink_to(xnu / "bsd/crypto/des/des.h")
        # Keep XNU's BSD-before-osfmk machine wrappers. Pointing machine/ at
        # osfmk/i386 wrongly selects its non-BSD endian header for network code.
        (overlay / "mach_debug").symlink_to(xnu / "osfmk/mach_debug", target_is_directory=True)
        # The shipping SDK intentionally omits native SMB's private kernel
        # contracts. Prefer the supplied actual kernel source to its redacted
        # public copies; keep generated architecture/MIG headers in the SDK.
        private = [overlay, xnu / "bsd", xnu / "osfmk", xnu / "libkern", xnu / "pexpert"]
        flags[0:0] = [flag for directory in private for flag in ("-I", str(directory))]
        flags.append("-DPRIVATE")
    results = [];inputs=[]
    for phase in target["buildPhases"]:
        phase = objects[phase]
        if phase["isa"] != "PBXSourcesBuildPhase":
            continue
        for key in phase["files"]:
            entry = objects[key]
            unit = resolve(entry["fileRef"])
            if unit.suffix not in (".c", ".cpp"):
                continue
            command = [str(cc), *flags, "-MD", "-MF",str(output/(unit.stem+".d")),
                       "-c", str(unit.relative_to(source)), "-o", str(output / (unit.stem + ".o"))]
            print(shlex.join(command), flush=True)
            # Native upstream assertions retain __FILE__. Compile project units
            # relative to their vendored root, so the distributable kernel
            # object never embeds the builder's username or checkout path.
            result = subprocess.run(command, cwd=source, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (output / (unit.stem + ".log")).write_bytes(result.stdout)
            results.append({"Unit": str(unit.relative_to(source)), "ExitCode": result.returncode})
            if result.returncode==0:inputs.append(output/(unit.stem+".o"))
            print(json.dumps(results[-1]), flush=True)
    (output / "Compilation.json").write_text(json.dumps(results, indent=2) + "\n")
    if any(item["ExitCode"] for item in results):return 1
    if args.link:
        info=(plistlib.loads(target["productSettingsXML"].encode()) if version=="10.4"
              else plistlib.loads((source/"Info-smbfs_kext.plist").read_bytes()))
        module=output/"module.c"
        module.write_text('#include <mach/vm_types.h>\n#include <mach/kmod.h>\n'
            'extern kern_return_t _start(kmod_info_t *,void *);\n'
            'extern kern_return_t _stop(kmod_info_t *,void *);\n'
            'extern kern_return_t smbfs_module_start(kmod_info_t *,void *);\n'
            'extern kern_return_t smbfs_module_stop(kmod_info_t *,void *);\n'
            'KMOD_EXPLICIT_DECL(com.apple.filesystems.smbfs,"'+info["CFBundleVersion"]+'",_start,_stop)\n'
            '__private_extern__ kmod_start_func_t *_realmain = smbfs_module_start;\n'
            '__private_extern__ kmod_stop_func_t *_antimain = smbfs_module_stop;\n'
            '__private_extern__ int _kext_apple_cc = __APPLE_CC__;\n')
        subprocess.run([str(cc),*flags,"-MD","-MF",str(output/"module.d"),
                        "-c",str(module),"-o",str(output/"module.o")],check=True)
        runtime=Path(subprocess.check_output([str(cc),"-m32","-mmacosx-version-min="+version,
            "-print-file-name=libgcc_static.a"],text=True).strip())
        if not runtime.is_file():parser.error("The period compiler static runtime is missing")
        bundle=output/"smbfs.kext/Contents";(bundle/"MacOS").mkdir(parents=True)
        executable=bundle/"MacOS/smbfs"
        subprocess.run([str(cc),"-m32","-B"+str(binutils)+"/","-isysroot",str(sdk),
            "-mmacosx-version-min="+version,"-nostdlib","-Xlinker","-r","-Xlinker","-no_uuid",
            *map(str,inputs),str(output/"module.o"),"-lkmodc++","-lkmod",str(runtime),
            "-o",str(executable)],check=True)
        (bundle/"Info.plist").write_bytes(plistlib.dumps(info))
        root=Path(__file__).resolve().parents[1]
        runpy.run_path(str(root/"scripts/build-opengl-kernel.py"))["verify_period_object"](executable,version)
    if args.vendor_headers:
        if not args.xnu:parser.error("--vendor-headers requires the actual --xnu source")
        destination=args.vendor_headers.resolve();destination.mkdir(parents=True,exist_ok=False)
        used=set()
        for dep in output.glob('*.d'):
            text=dep.read_text().replace('\\\n','')
            for name in shlex.split(text.split(':',1)[1]):
                path=Path(name)
                path=(path if path.is_absolute() else source/path).resolve()
                if path.is_relative_to(xnu):used.add(path)
        if not used:raise ValueError('No actual XNU dependencies were emitted')
        for path in sorted(used):
            target=destination/path.relative_to(xnu);target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,target)
        shutil.copyfile(xnu/'APPLE_LICENSE',destination/'APPLE_LICENSE')
        print('Vendored actual XNU headers:',len(used),flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
