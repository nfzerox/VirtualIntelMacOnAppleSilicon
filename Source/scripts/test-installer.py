#!/usr/bin/env python3
"""Exercise installer transactions in a throwaway tree, never the host OS.

Only this test copy rewrites absolute system paths and injects fake platform,
signature and cache tools. Production has no test/root-prefix escape hatch.
"""
import os
import re
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

repo = Path(__file__).resolve().parent.parent

def case(scenario, platform):
    with tempfile.TemporaryDirectory(prefix="pvg-installer-test-") as directory:
        base = Path(directory).resolve()
        root = base / "system"
        testrepo = base / "repo"
        (testrepo / "scripts").mkdir(parents=True)
        shutil.copy(repo / "scripts/pvg-platform", testrepo / "scripts/pvg-platform")
        shutil.copytree(repo / "support", testrepo / "support")
        mojave = platform.startswith('10.14.')
        release = testrepo / ("prebuilt/mojave" if mojave else "prebuilt")
        (release / "probes").mkdir(parents=True)
        compatibility = release / "probes/guest-compatibility"
        compatibility.write_text("#!/bin/sh\nexit 0\n")
        compatibility.chmod(0o755)
        (testrepo / "scripts/pvg-stage-guest").write_text("#!/bin/zsh\nexit %d\n" % (65 if scenario=='patch-fail' else 0))
        targets = ["/System/Library/test-driver", "/Library/test-helper"]
        rewritten = [str(root) + p for p in targets]
        (testrepo / "support/install-targets.txt").write_text("\n".join(rewritten) + "\n")
        (testrepo / "support/install-targets-mojave.txt").write_text("\n".join(rewritten) + "\n")
        payload = base / "payload"
        for target in rewritten:
            file = payload / target.lstrip("/")
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("new driver")
        original = Path(rewritten[0])
        original.parent.mkdir(parents=True, exist_ok=True)
        original.write_text("original driver")
        caches = [root / "System/Library/PrelinkedKernels/prelinkedkernel",
                  root / "Library/Apple/System/Library/PrelinkedKernels/prelinkedkernel"]
        for cache in caches:
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text("original cache")
        (root / "Library/Extensions").mkdir(parents=True)
        (root / "System/Library/Extensions").mkdir(parents=True)
        (root / "var/run").mkdir(parents=True)
        for relative in ("System/Library/Frameworks/CoreDisplay.framework/Versions/A/CoreDisplay",
                         "System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/3902/MTLCompiler",
                         "System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc/Contents/MacOS/MTLCompilerService"):
            native=root/relative; native.parent.mkdir(parents=True,exist_ok=True); native.write_text("native fixture")
        with tarfile.open(release / "guest-payload.tar.gz", "w:gz") as archive:
            archive.add(payload, arcname="payload")
        # shasum remains real; only the small fixture archive is hashed.
        identity = 'Guest: Intel Mojave 10.14.x' if mojave else 'Guest: Intel Catalina 10.15.x'
        (release/'RELEASE.txt').write_text('Guest: Intel WrongOS 99.0\n' if scenario=='wrong-os' else identity+'\n')
        digest = subprocess.check_output(["shasum", "-a", "256", "guest-payload.tar.gz", "RELEASE.txt"], cwd=release)
        (release / "SHA256SUMS").write_bytes(digest)
        if scenario == "corrupt":
            with (release / "guest-payload.tar.gz").open("ab") as stream:
                stream.write(b"damaged copy")
        script = (repo / "scripts/pvg-setup").read_text()
        script = re.sub(r"(?<![A-Za-z0-9])(?:/System/|/Library/|/var/run/)", lambda m: str(root) + m.group(), script)
        script = script.replace("if (( EUID != 0 )); then", "if false; then")
        script = script.replace('/usr/sbin/sysctl -n kern.bootargs', 'sysctl -n kern.bootargs')
        script = script.replace('exec /usr/sbin/kextcache', 'kextcache')
        mocks = r'''
uname() { print x86_64; }
sw_vers() { if [[ ${1:-} == -productVersion ]]; then print "$TEST_PLATFORM"; else print "$TEST_BUILD"; fi; }
ioreg() { print '\"model\" = <\"Parallels26,1\">'; }
csrutil() { print "System Integrity Protection status: ${TEST_SIP:-disabled}."; }
mount() { return 0; }
chown() { return 0; }
codesign() { return 0; }
df() { print 'fake 9000000 1000 8000000 /'; }
sysctl() { [[ $TEST_SCENARIO == missing-amfi ]] || print 'amfi_get_out_of_my_way=0x1'; return 0; }
nvram() {
    if [[ $1 == boot-args ]]; then print 'boot-args\tkeepsyms=1';
    else print -r -- "$*" > "$TEST_BASE/nvram-last"; fi
}
kextcache() {
    [[ $TEST_SCENARIO != fail-cache && $TEST_SCENARIO != fail-restore ]] || return 23
    /bin/sleep 1
    if [[ $TEST_PLATFORM == 10.14.* ]]; then
        print 'new cache' > "$TEST_ROOT/System/Library/PrelinkedKernels/prelinkedkernel"
    else
        print 'new cache' > "$TEST_ROOT/Library/Apple/System/Library/PrelinkedKernels/prelinkedkernel"
    fi
}
mv() {
    if [[ $TEST_SCENARIO == fail-restore && $2 == *-restore/* ]]; then return 19; fi
    command mv "$@"
}
'''
        script = script.replace("mode=${1:-install}", mocks + "\nmode=${1:-install}")
        entry = testrepo / "scripts/pvg-setup"
        entry.write_text(script)
        env = dict(os.environ, TEST_BASE=str(base), TEST_ROOT=str(root), TEST_SCENARIO=scenario, TEST_PLATFORM=platform,
                   TEST_BUILD='18G9999' if mojave else '19H9999')
        if scenario == "sip-enabled":
            env["TEST_SIP"] = "enabled"
        result = subprocess.run(["/bin/zsh", str(entry)], input="INSTALL\n", text=True, env=env, capture_output=True)
        if scenario == "success":
            assert result.returncode == 0, result.stdout + result.stderr
            assert original.read_text() == "new driver"
            assert Path(rewritten[1]).read_text() == "new driver"
            assert caches[0].read_text().strip() == "new cache"
            backups = list((root / "Library/Application Support/PVGBackport/backups").iterdir())
            rolled = subprocess.run(["/bin/zsh", str(entry), "--rollback", str(backups[0])],
                                    input="RESTORE\n", text=True, env=env, capture_output=True)
            assert rolled.returncode == 0, rolled.stdout + rolled.stderr
        else:
            assert result.returncode != 0, result.stdout + result.stderr
            if scenario == "fail-restore":
                assert "Automatic rollback failed" in result.stderr, result.stdout + result.stderr
                assert "Original files and boot caches restored" not in result.stderr
                assert original.read_text() == "new driver"
                assert not (base / "nvram-last").exists()
                print("PASS", platform, scenario)
                return
            if scenario == "fail-cache":
                assert "Original files and boot caches restored" in result.stderr, result.stdout + result.stderr
            elif scenario == "missing-amfi":
                assert "Boot flags" in result.stderr, result.stdout + result.stderr
            elif scenario == "sip-enabled":
                assert "SIP is not fully disabled" in result.stderr, result.stdout + result.stderr
            elif scenario == "wrong-os":
                assert "Release targets a different guest OS" in result.stderr, result.stdout + result.stderr
            elif scenario == "patch-fail":
                assert not (root / 'Library/Application Support/PVGBackport/backups').exists()
            else:
                assert "Release is incomplete or corrupt" in result.stderr, result.stdout + result.stderr
        assert original.read_text() == "original driver", result.stdout + result.stderr
        assert not Path(rewritten[1]).exists()
        assert all(p.read_text() == "original cache" for p in caches)
        assert not (root / "var/run/pvgbackport-install.lock").exists()
        assert not (base / "nvram-last").exists()
        print("PASS", platform, scenario)

for platform in ("10.14.5", "10.14.6", "10.15.6", "10.15.7"):
    for scenario in ("sip-enabled", "missing-amfi", "corrupt", "wrong-os", "patch-fail", "success", "fail-cache", "fail-restore"):
        case(scenario, platform)
