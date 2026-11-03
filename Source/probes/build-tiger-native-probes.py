#!/usr/bin/env python3
"""Build isolated i386 Tiger controls with the genuine SDK and period linker.

No VM/system installation or execution. Modern ld rejects this SDK's actual
prebound frameworks; Clang's driver adds an unsupported -lto_library to ld97.
Compile normally, then invoke the explicit compatible period linker directly.
"""
import argparse
import hashlib
from pathlib import Path
import plistlib
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("sdk", "cc", "ld", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument('--graphics-only',action='store_true',help='Only isolated native CGL/GLI survey programs')
    args = parser.parse_args()
    sdk, cc, linker, output = (getattr(args, key).resolve() for key in ("sdk", "cc", "ld", "output"))
    if plistlib.loads((sdk / "SDKSettings.plist").read_bytes()).get("Version") != "10.4":
        parser.error("Supply the genuine MacOSX10.4u SDK")
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    inputs = [root / "probes/opengl-leopard-public-pbuffer.c", root / "probes/opengl-tiger-native-contract.c",
              root / 'probes/opengl-tiger-gli-records.c', root / 'probes/tiger-private-code-page.c',
              root / 'probes/tiger-display-pointer.c', root / 'probes/opengl-period-window-drag.c',
              root / 'probes/opengl-gl-roundtrip.c',root / 'probes/opengl-panel-clock.c']
    def run(command):
        print(shlex.join(map(str, command)), flush=True)
        subprocess.run(list(map(str, command)), check=True)
    products = {}
    for unit, name in zip(inputs, ("public-pbuffer", "native-contract", 'gli-records', 'page-permissions', 'display-pointer', 'period-window-drag', 'gl-roundtrip', 'panel-clock')):
        obj = output / (name + ".o")
        commands = [
            [cc, "-arch", "i386", "-mmacosx-version-min=10.4", "-isysroot", sdk,
             "-O2", "-Wall", "-Wextra", "-Werror", "-c", unit, "-o", obj],
            [linker, "-arch", "i386", "-macosx_version_min", "10.4", "-syslibroot", sdk,
             "-no_uuid", "-lcrt1.o", obj, "-framework", "OpenGL", "-framework", "ApplicationServices", "-framework", "CoreFoundation", "-framework", "IOKit",
             "-lSystem", "-o", output / name],
        ]
        for command in commands:
            run(command)
        products[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
    if args.graphics_only:
        receipt={'FormatVersion':1,'Architecture':'i386','MinimumOS':'10.4','GraphicsSurveyOnly':True,
                 'Files':products,'SourceFiles':{str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in inputs+[Path(__file__).resolve()]}}
        (output/'BuildReceipt.plist').write_bytes(plistlib.dumps(receipt));return
    foundation = [root / name for name in ("probes/tiger-period-foundation.m",
                  "tools/opengl-period-foundation.m", "vendor/source/JSONKit/JSONKit.m")]
    objects = []
    for unit in foundation:
        obj = output / (unit.stem + ".o")
        run([cc, "-arch", "i386", "-mmacosx-version-min=10.4", "-isysroot", sdk,
             "-O2", "-fno-objc-arc", "-Wall", "-Wextra", "-Werror",
             "-Wno-deprecated-objc-isa-usage", "-Wno-deprecated-objc-pointer-introspection",
             "-c", unit, "-o", obj])
        objects.append(obj)
    # The SDK's real Tiger GCC support dylib supplies i386 64-bit integer
    # division, without importing a later compiler runtime or bundling one.
    run([linker, "-arch", "i386", "-macosx_version_min", "10.4", "-syslibroot", sdk,
         "-no_uuid", "-lcrt1.o", *objects, "-framework", "Foundation", "-framework", "CoreFoundation",
         "-lgcc_s.10.4", "-lSystem", "-o", output / "foundation-control"])
    products["foundation-control"] = hashlib.sha256((output / "foundation-control").read_bytes()).hexdigest()
    inputs += foundation + [root / "tools/opengl-period-foundation.h", root / "vendor/source/JSONKit/JSONKit.h"]
    for unit, name in ((root / "tools/opengl-guest-agent.m", "opengl-guest-agent"),
                       (root / "probes/opengl-period-agent-control.m", "agent-control"),
                       (root / "tools/opengl-tiger-install-config.m", "opengl-tiger-install-config")):
        obj=output / (name + ".o")
        run([cc, "-arch", "i386", "-mmacosx-version-min=10.4", "-isysroot", sdk,
             "-O2", "-fno-objc-arc", "-Wall", "-Wextra", "-Werror",
             "-include", root / "tools/opengl-period-foundation.h", "-c", unit, "-o", obj])
        run([linker, "-arch", "i386", "-macosx_version_min", "10.4", "-syslibroot", sdk,
             "-no_uuid", "-lcrt1.o", obj, *objects[1:], "-framework", "Foundation", "-framework", "IOKit",
             "-framework", "CoreFoundation", "-framework", "DiskArbitration", "-lgcc_s.10.4", "-lSystem", "-o", output / name])
        products[name]=hashlib.sha256((output / name).read_bytes()).hexdigest()
        inputs.append(unit)
    inputs += [root / ("tools/" + name) for name in ("opengl-boot-identity.h", "opengl-guest-machine.h", "opengl-agent-framing.h")]
    unit=root / "tools/opengl-tiger-installer-bootstrap.c"
    obj=output / "installer-bootstrap.o"
    run([cc, "-arch", "i386", "-mmacosx-version-min=10.4", "-isysroot", sdk,
         "-O2", "-Wall", "-Wextra", "-Werror", "-c", unit, "-o", obj])
    name="opengl-tiger-installer-bootstrap"
    run([linker, "-arch", "i386", "-macosx_version_min", "10.4", "-syslibroot", sdk,
         "-no_uuid", "-lcrt1.o", obj, "-framework", "IOKit", "-framework", "CoreFoundation",
         "-lSystem", "-o", output / name])
    products[name]=hashlib.sha256((output / name).read_bytes()).hexdigest();inputs.append(unit)
    unit=root / "probes/tiger-directory-contract.c"
    obj=output / "directory-contract.o"
    run([cc,"-arch","i386","-mmacosx-version-min=10.4","-isysroot",sdk,
         "-O2","-Wall","-Wextra","-Werror","-c",unit,"-o",obj])
    name="directory-contract"
    run([linker,"-arch","i386","-macosx_version_min","10.4","-syslibroot",sdk,
         "-no_uuid","-lcrt1.o",obj,"-framework","DirectoryService","-lSystem","-o",output/name])
    products[name]=hashlib.sha256((output/name).read_bytes()).hexdigest();inputs.append(unit)
    unit=root/"probes/tiger-native-services.m";obj=output/"native-services.o"
    run([cc,"-arch","i386","-mmacosx-version-min=10.4","-isysroot",sdk,
         "-O2","-fno-objc-arc","-Wall","-Wextra","-Werror","-c",unit,"-o",obj])
    name="native-services"
    run([linker,"-arch","i386","-macosx_version_min","10.4","-syslibroot",sdk,
         "-no_uuid","-lcrt1.o",obj,"-framework","Foundation","-framework","CoreFoundation",
         "-framework","IOKit","-framework","SecurityFoundation","-framework","Security","-lSystem","-o",output/name])
    products[name]=hashlib.sha256((output/name).read_bytes()).hexdigest();inputs.append(unit)
    account=[]
    for relative in ("tools/opengl-create-account.m","tools/opengl-leopard-directory.m"):
        unit=root/relative;obj=output/(unit.stem+".o")
        run([cc,"-arch","i386","-mmacosx-version-min=10.4","-isysroot",sdk,
             "-O2","-fno-objc-arc","-Wall","-Wextra","-Werror",
             "-include",root/"tools/opengl-period-foundation.h","-c",unit,"-o",obj])
        account.append(obj);inputs.append(unit)
    name="opengl-create-account"
    run([linker,"-arch","i386","-macosx_version_min","10.4","-syslibroot",sdk,
         "-no_uuid","-lcrt1.o",*account,*objects[1:],"-framework","Foundation",
         "-framework","DirectoryService","-framework","DiskArbitration","-framework","IOKit",
         "-framework","CoreFoundation","-lgcc_s.10.4","-lSystem","-o",output/name])
    products[name]=hashlib.sha256((output/name).read_bytes()).hexdigest()
    inputs += [root/("tools/"+name) for name in ("opengl-account-directory.h","opengl-leopard-directory.h",
               "opengl-login-policy.h","opengl-first-login.h","opengl-state-path.h","opengl-launchd-state.h")]
    receipt = {"FormatVersion": 1, "Architecture": "i386", "MinimumOS": "10.4", "Files": products,
               "SourceFiles": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in inputs + [Path(__file__).resolve()]}}
    (output / "BuildReceipt.plist").write_bytes(plistlib.dumps(receipt))


if __name__ == "__main__":
    main()
