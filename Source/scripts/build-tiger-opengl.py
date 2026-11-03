#!/usr/bin/env python3
"""Build an isolated native Tiger/i386 PassthroughOpenGL candidate.

Canonical transport declarations come from the audited Mavericks SDK; the
dispatch overlay comes from a named, runtime-audited period SDK; all linked
system libraries come from the genuine Tiger SDK. No guest or published payload is modified. This is not a qualification
receipt: the resulting pair still requires execution on its target system.
"""
import argparse
import hashlib
import os
import json
from pathlib import Path
import plistlib
import re
import shlex
import subprocess
import sys


def method_kinds(root, output):
    """Derive native MIG calls from the owned kernel endpoint, not call shape."""
    source = root / "vendor/source/VMsvga2/AC/UC/VMsvga22DContext.cpp"
    selectors = root / "vendor/source/VMsvga2/AC/UCMethods.h"
    strip = lambda text: re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    enum = re.search(r"enum eIOVM2DMethods\s*\{(.*?)\};", strip(selectors.read_text()), re.S)
    table = re.search(r"iofbFuncsCache\[kIOVM2DNumMethods\]\s*=\s*\{(.*?)\n\};",
                      strip(source.read_text()), re.S)
    if not enum or not table:
        raise ValueError("The owned 2D endpoint declaration changed")
    names = [name.strip() for name in enum[1].split(",") if name.strip()]
    if names[-1] != "kIOVM2DNumMethods" or any(not re.fullmatch(r"kIOVM2D\w+", n) for n in names):
        raise ValueError("The 2D selectors must be contiguous and implicitly numbered")
    names.pop()
    entries = re.findall(r"\{([^{}]+)\}", table[1])
    values = {"kIOUCScalarIScalarO": 0, "kIOUCScalarIStructO": 2,
              "kIOUCStructIStructO": 3, "kIOUCScalarIStructI": 4}
    kinds = []
    for entry in entries:
        matches = [name for name in re.findall(r"\bkIOUC\w+", entry)
                   if name != "kIOUCVariableStructureSize"]
        if len(matches) != 1 or matches[0] not in values:
            raise ValueError("Unsupported 2D method kind; do not guess a MIG entry point")
        kinds.append(values[matches[0]])
    if len(kinds) != len(names):
        raise ValueError("2D selector/dispatch counts disagree")
    lines = ["/* Generated from the owned VMsvga22DContext method table. */",
             "static inline unsigned VIMTigerMethodKind(unsigned selector)", "{",
             "    static const unsigned char kinds[]={"]
    lines += [f"        {kind}, /* {name} */" for name, kind in zip(names, kinds)]
    lines += ["    };", "    return selector<sizeof(kinds)?kinds[selector]:255;", "}", ""]
    (output / "tiger-iokit-methods-generated.inc").write_text("\n".join(lines))
    return [source, selectors]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("sdk", "canonical-sdk", "cc", "ld", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument('--dispatch-sdk',type=Path,help='Named759-entry layout audited against the8R4088 runtime')
    parser.add_argument('--runtime-dispatch-count',type=int,choices=(686,759),required=True)
    parser.add_argument('--native-engine',type=Path,help='Measured target stock engine, required to build explicit candidate deployment tool')
    args = parser.parse_args()
    sdk, canonical, cc, linker, output = (getattr(args, key).resolve() for key in
                                         ("sdk", "canonical_sdk", "cc", "ld", "output"))
    if plistlib.loads((sdk / "SDKSettings.plist").read_bytes()).get("Version") != "10.4":
        parser.error("Supply the genuine MacOSX10.4u SDK")
    if not (canonical / "System/Library/Frameworks/OpenGL.framework/Headers/gliDispatch.h").is_file():
        parser.error("Supply the audited canonical OpenGL SDK")
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    commands = []

    def run(command):
        command = list(map(str, command))
        commands.append(command)
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True)

    run([sys.executable, root / "scripts/generate-passthrough-opengl.py", canonical, output])
    dispatch_sdk=args.dispatch_sdk.resolve() if args.dispatch_sdk else sdk
    generation=[sys.executable, root / "scripts/generate-opengl-gli.py",
         output / "passthrough-opengl-gli-map.json", output / "passthrough-opengl-gli-generated.inc",
         "--dispatch-sdk", dispatch_sdk]
    if args.runtime_dispatch_count==759:generation+=['--runtime-dispatch-count',759]
    run(generation)
    if f"#define VIM_GLI_DISPATCH_COUNT {args.runtime_dispatch_count}" not in (output / "passthrough-opengl-gli-generated.inc").read_text():
        raise ValueError("Missing measured native Tiger dispatch table")
    if args.runtime_dispatch_count==759:
        # One renderer supports both measured native Tiger interfaces. The
        # shorter output must be the genuine SDK's NAMED prototype prefix,
        # never a guessed byte count into a reordered dispatch table.
        fields=lambda text:re.findall(r'\(\s*\*\s*(\w+)\)\s*\(',text)
        early=fields((sdk/'System/Library/Frameworks/OpenGL.framework/Headers/gliDispatch.h').read_text())
        full=fields((output/'PeriodABI/OpenGL/gliDispatch.h').read_text())
        if len(early)!=686 or full[:686]!=early:
            raise ValueError('The genuine686-slot Tiger table is not the generated759-slot prefix')
    inputs = method_kinds(root, output)
    flags = ["-arch", "i386", "-mmacosx-version-min=10.4", "-isysroot", canonical,
             "-I", output / "PeriodABI", "-I", output, "-O2", "-g", "-fno-omit-frame-pointer",
             "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations", "-Wno-unused-function",
             "-Wno-missing-braces", "-Wno-missing-field-initializers", "-fno-builtin-memchr"]
    link = [linker, "-arch", "i386", "-macosx_version_min", "10.4", "-syslibroot", sdk, "-no_uuid"]
    client = root / "PassthroughOpenGL/passthrough-opengl-client.c"
    engine = root / "PassthroughOpenGL/passthrough-opengl-gli.c"
    run([cc, *flags, "-c", client, "-o", output / "client.o"])
    os.utime(output / "client.o", (0,0))  # deterministic period ld N_OSO debug-map timestamp
    run([*link, "-dylib", output / "client.o", "-install_name", "@loader_path/libVIMPassthroughOpenGL.dylib",
         "-framework", "IOKit", "-framework", "CoreFoundation", "-lgcc_s.10.4", "-lSystem",
         "-o", output / "libVIMPassthroughOpenGL.dylib"])
    run([cc, *flags, "-DVIM_PASSTHROUGH_OPENGL_BACKEND=1",
         "-fvisibility=hidden", "-c", engine, "-o", output / "gli.o"])
    os.utime(output / "gli.o", (0,0))
    run([*link, "-bundle", output / "gli.o", output / "libVIMPassthroughOpenGL.dylib",
         "-exported_symbols_list", root / "PassthroughOpenGL/gli.exports", "-framework", "IOKit",
         "-framework", "CoreFoundation", "-framework", "ApplicationServices", "-lgcc_s.10.4", "-lSystem",
         "-o", output / "GLEngine"])
    probe=root / "probes/opengl-tiger-candidate.c"
    run([cc,"-arch","i386","-mmacosx-version-min=10.4","-isysroot",sdk,
         '-I',output/'PeriodABI',f'-DVIM_TIGER_RUNTIME_SLOTS={args.runtime_dispatch_count}',
         "-O2","-Wall","-Wextra","-Werror","-c",probe,"-o",output / "candidate.o"])
    run([*link,"-lcrt1.o",output / "candidate.o","-framework","IOKit",
         "-framework","CoreFoundation","-lSystem","-o",output / "candidate"])
    inputs.append(probe)
    # The real period GA declaration owns its function-table size/signatures.
    # Do not donate a later opaque plugin or a foreign C++ runtime to Tiger.
    vendor=root / 'vendor/source/VMsvga2'
    ga_flags=['-arch','i386','-mmacosx-version-min=10.4','-isysroot',sdk,'-Os',
              '-fvisibility=hidden','-DLOGGING_LEVEL=0',
              '-DSUPPORT_CGLS=1','-I',vendor/'AC','-I',vendor/'vmutil',
              '-I',root/'PassthroughOpenGL','-I',output,
              '-include',root/'PassthroughOpenGL/iokit-bridge-compat.h']
    cxx=Path(str(cc)+'++')
    if not cxx.is_file():raise ValueError('The selected compiler has no companion clang++')
    run([cxx,*ga_flags,'-std=gnu++98','-fno-exceptions','-fno-rtti','-nostdinc++',
         '-isystem',sdk/'usr/include/c++/4.0.0','-isystem',sdk/'usr/include/c++/4.0.0/i686-apple-darwin8',
         '-c',vendor/'GA/VMsvga2GA.cpp','-o',output/'GA.o'])
    ga_objects=[output/'GA.o']
    for unit in ('GA/BlitHelper','vmutil/VLog','vmutil/SendString'):
        obj=output/(Path(unit).name+'.o')
        run([cc,*ga_flags,'-c',vendor/(unit+'.c'),'-o',obj]);ga_objects.append(obj)
    plugin=output/'VMsvga2GA.plugin/Contents'
    (plugin/'MacOS').mkdir(parents=True)
    run([*link,'-bundle',*ga_objects,'-framework','CoreFoundation','-framework','IOKit',
         '-lSystem','-o',plugin/'MacOS/VMsvga2GA'])
    info=plistlib.loads((vendor/'Info-GA.plist').read_bytes())
    values={'${EXECUTABLE_NAME}':'VMsvga2GA','${PRODUCT_NAME}':'VMsvga2GA',
            '${MODULE_NAME}':'net.osx86.VMsvga2GA','${MODULE_VERSION}':'1.2.5',
            '${COPYRIGHT_NOTICE}':'Copyright 2009-2013 Zenith432; Virtual Intel Mac contributors'}
    info={key:values.get(value,value) if isinstance(value,str) else value for key,value in info.items()}
    (plugin/'Info.plist').write_bytes(plistlib.dumps(info))
    if args.native_engine:
        if args.runtime_dispatch_count!=759:raise ValueError('Candidate deployment is qualified only for the8R4088 ABI')
        native=args.native_engine.resolve()
        if not native.is_file():raise ValueError('The actual target stock engine is missing')
        objects=[]
        for unit in ('tools/opengl-tiger-graphics.m','tools/opengl-period-foundation.m','vendor/source/JSONKit/JSONKit.m'):
            path=root/unit;obj=output/(path.stem+'.o');objects.append(obj);inputs.append(path)
            run([cc,'-arch','i386','-mmacosx-version-min=10.4','-isysroot',sdk,'-O2','-fno-objc-arc',
                 '-Wall','-Wextra','-Werror','-Wno-deprecated-objc-isa-usage','-Wno-deprecated-objc-pointer-introspection',
                 '-include',root/'tools/opengl-period-foundation.h','-c',path,'-o',obj])
        run([*link,'-lcrt1.o',*objects,'-framework','Foundation','-framework','CoreFoundation','-framework','IOKit',
             '-framework','DiskArbitration','-lgcc_s.10.4','-lSystem','-o',output/'opengl-tiger-graphics'])
        inputs += [root/'tools'/name for name in ('opengl-period-foundation.h','opengl-guest-machine.h','opengl-state-path.h','opengl-boot-identity.h')]
        inputs.append(root/'vendor/source/JSONKit/JSONKit.h')
    # Keep the compiled GA units plus their conservative header closure. Kernel
    # implementation files are not linked here: treating FB/VMsvga2.cpp as a
    # GL input needlessly rebuilt/reinstalled the entire user-space payload for
    # every framebuffer-only fix (and old ld's debug timestamps changed bytes).
    inputs += [p for p in vendor.rglob('*.h') if p.is_file()]
    inputs += [vendor/name for name in ('GA/VMsvga2GA.cpp','GA/BlitHelper.c',
                                        'vmutil/VLog.c','vmutil/SendString.c',
                                        'AC/UC/VMsvga22DContext.cpp')]
    inputs.append(vendor/'Info-GA.plist')
    inputs += sorted(p for p in (root / "PassthroughOpenGL").iterdir() if p.is_file())
    inputs += [Path(__file__).resolve(), root / "scripts/generate-opengl-gli.py",
               root / "scripts/generate-passthrough-opengl.py"]
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    receipt = {"FormatVersion": 1, "Architecture": "i386", "MinimumOS": "10.4",
               "DispatchCount": args.runtime_dispatch_count, "RuntimeQualified": False,
               "NativeDispatchCounts": [686,759] if args.runtime_dispatch_count==759 else [686],
               "Files": {name: True for name in ("GLEngine", "libVIMPassthroughOpenGL.dylib", "candidate",
                         'VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA','VMsvga2GA.plugin/Contents/Info.plist')}}
    if args.native_engine:
        receipt['NativeEngineSHA256']=digest(args.native_engine)
        receipt['NativePrebootInstallVersion']=1
        receipt['Files']['opengl-tiger-graphics']=True
    (output / "BuildReceipt.plist").write_bytes(plistlib.dumps(receipt))
    (output / "BuildCommands.json").write_text(json.dumps(commands, indent=2) + "\n")


if __name__ == "__main__":
    main()
