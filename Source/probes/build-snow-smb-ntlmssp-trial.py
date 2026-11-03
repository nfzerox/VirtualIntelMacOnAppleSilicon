#!/usr/bin/env python3
"""Build a private Snow Leopard kext with the Leopard NTLMv2 codec; no install."""
import argparse
import json
from pathlib import Path
import plistlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "sdk", "xnu", "cc", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    source, sdk, xnu, cc, output = (
        getattr(args, name).resolve() for name in ("source", "sdk", "xnu", "cc", "output"))
    metadata = (source / "Info-smbfs_kext.plist").read_bytes()
    info = plistlib.loads(metadata)
    if info["CFBundleVersion"] != "1.6.6":
        parser.error("The 10J3250 trial requires the native 1.6.6 source (smb-431.9)")
    if plistlib.loads((sdk / "SDKSettings.plist").read_bytes())["Version"] != "10.6":
        parser.error("Supply the genuine 10.6 SDK")
    output.mkdir(parents=True, exist_ok=False)
    here = Path(__file__).resolve().parent
    codec = here.parent / "vendor/source/smb-348.7/kernel/netsmb"
    kernel = sdk / "System/Library/Frameworks/Kernel.framework"
    private = output / "PrivateHeaders"
    (private / "crypto").mkdir(parents=True)
    (private / "crypto/des.h").symlink_to(xnu / "bsd/crypto/des/des.h")
    target = ["-target", "x86_64-apple-macos10.6", "-isysroot", str(sdk)]
    flags = target + ["-nostdinc", "-mkernel", "-fno-builtin", "-fno-common", "-Os",
        "-DKERNEL", "-DKERNEL_PRIVATE", "-DDRIVER_PRIVATE", "-DAPPLE", "-DNeXT",
        "-D__APPLE_API_UNSTABLE", "-DPRIVATE", "-DNAMEDSTREAMS"]
    for directory in (private, xnu/"bsd", xnu/"osfmk", xnu/"libkern", xnu/"pexpert",
                      source/"kernel", source/"kernel/sys5", source/"kernel/mysys", source/"kernel/fs",
                      kernel/"PrivateHeaders", kernel/"Headers", codec, here):
        flags += ["-I", str(directory)]
    objects = json.loads(subprocess.check_output([
        "plutil", "-convert", "json", "-o", "-", str(source/"smb.xcodeproj/project.pbxproj")]))["objects"]
    parents = {child: key for key, item in objects.items() if item.get("isa") == "PBXGroup"
               for child in item.get("children", [])}

    def path(key):
        item = objects[key]
        parent = parents.get(key) if item.get("sourceTree") != "SOURCE_ROOT" else None
        return (path(parent) if parent else source) / item.get("path", "")

    project_target = next(item for item in objects.values()
                          if item.get("isa") == "PBXNativeTarget" and item.get("name") == "smbfs.kext")
    units = [path(objects[key]["fileRef"]) for phase in project_target["buildPhases"]
             if objects[phase]["isa"] == "PBXSourcesBuildPhase" for key in objects[phase]["files"]]
    units = [unit for unit in units if unit.suffix in (".c", ".cpp")]
    wrapper = output / "smb_gss_trial.c"
    wrapper.write_text("""#define smb_gss_ssnsetup smb_gss_ssnsetup_stock
#include <netsmb/smb_gss.c>
#undef smb_gss_ssnsetup
#include "snow-smb-leopard-ntlmssp.h"
int smb_gss_ssnsetup(struct smb_vc *vc, vfs_context_t ctx)
{
    if (vc->vc_username && vc->vc_username[0] && smb_vc_getpass(vc)[0] &&
        (vc->vc_flags & SMBV_SIGNING) && SMB_USE_GSS(vc) &&
        !(vc->vc_flags & (SMBV_KERBEROS_ACCESS | SMBV_GUEST_ACCESS |
                          SMBV_ANONYMOUS_ACCESS | SMBV_NTLMV2_OFF)) &&
        (vc->vc_flags & SMBV_MINAUTH) < SMBV_MINAUTH_KERBEROS) {
        vc->vc_smbuid = 0;
        return trial_leopard_ntlmssp(vc, ctx, smb_gss_vc_caps(vc));
    }
    return smb_gss_ssnsetup_stock(vc, ctx);
}
""")
    module = output / "module.c"
    module.write_text("""#include <mach/vm_types.h>
#include <mach/kmod.h>
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(com.apple.filesystems.smbfs,"1.6.6",_start,_stop)
extern kern_return_t smbfs_module_start(kmod_info_t *, void *);
extern kern_return_t smbfs_module_stop(kmod_info_t *, void *);
__private_extern__ kmod_start_func_t *_realmain = smbfs_module_start;
__private_extern__ kmod_stop_func_t *_antimain = smbfs_module_stop;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
""")
    inputs = []
    for unit in [*units, codec/"vim_ntlmssp_wire.c", module]:
        if unit.name == "smb_gss.c":
            unit = wrapper
        obj = output/(unit.stem + ".o")
        log = output/(unit.stem + ".log")
        language = ["-std=gnu++98", "-fno-rtti", "-fno-exceptions"] if unit.suffix == ".cpp" else []
        with log.open("w") as handle:
            result = subprocess.run([cc, *flags, *language, "-c", unit, "-o", obj],
                                    cwd=source, stdout=handle, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(f"Compile failed: {log}")
        inputs.append(obj)
    bundle = output/"smbfs.kext/Contents"
    (bundle/"MacOS").mkdir(parents=True)
    # Snow's SDK archives align x86_64 members to four bytes. Repack the
    # genuine members for the current linker; don't substitute newer stubs.
    archives = []
    ar = cc.with_name("llvm-ar")
    for name in ("libkmodc++.a", "libkmod.a"):
        stage = output / name.removesuffix(".a")
        stage.mkdir()
        original = sdk/"usr/lib"/name
        thin = stage/"Original.a"
        subprocess.run(["lipo", original, "-thin", "x86_64", "-output", thin], check=True)
        subprocess.run([ar, "x", thin], cwd=stage, check=True)
        rebuilt = output/name
        subprocess.run([ar, "--format=darwin", "rcs", rebuilt, *sorted(stage.glob("*.o"))],
                       check=True)
        archives.append(rebuilt)
    subprocess.run([cc, *target, "-Xlinker", "-kext", "-nostdlib", *inputs,
                    *archives, "-o", bundle/"MacOS/smbfs"], check=True)
    (bundle/"Info.plist").write_bytes(metadata)
    print(bundle.parent)


if __name__ == "__main__":
    main()
