import io
from pathlib import Path
import plistlib
import sqlite3
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "darwin", "Uses the shipping native worker")
class TigerUpdatePreflightTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        work = tempfile.TemporaryDirectory()
        cls.addClassCleanup(work.cleanup)
        root = Path(work.name)
        cls.probe = root / "preflight"
        sources = [SOURCE / "probes/host-install-manual-account.m",
                   SOURCE / "tools/host-install-pbzx.c", SOURCE / "tools/host-install-bigsur-seal.c",
                   SOURCE / "tools/host-install-bigsur-digest.c"]
        sources += sorted(path for path in (SOURCE / "tools/HostInstall").glob("*.m") if path.name != "install-worker.m")
        subprocess.run(("xcrun", "clang", "-fobjc-arc", "-fobjc-exceptions", "-Wall", "-Wextra", "-Werror",
                        "-DVIM_BIGSUR_DIGEST_LIBRARY", "-I", SOURCE / "vendor/xz-api", "-framework", "Foundation",
                        "-framework", "Security", "-lsqlite3", "-lz", "-llzma.5", "-lcompression", "-larchive",
                        "-lAppleArchive", *sources, "-o", cls.probe), check=True)
        cls.private = root / "test-only.pem"
        key = root / "test-only.der"
        subprocess.run(("/usr/bin/openssl", "genrsa", "-out", cls.private, "2048"), check=True, capture_output=True)
        subprocess.run(("/usr/bin/openssl", "rsa", "-in", cls.private, "-RSAPublicKey_out", "-outform", "DER", "-out", key),
                       check=True, capture_output=True)
        cls.key = key.read_bytes()

    def outer(self, path, name, tamper=False):
        # Intentionally not an inner tar: a bad second signature must be
        # detected before the first authenticated package is even unpacked.
        payload = b"authenticated-but-not-an-inner-tar" * 128
        signature = subprocess.check_output(("/usr/bin/openssl", "dgst", "-sha1", "-sign", self.private), input=payload)
        record = name.encode() + b"\n" + signature.hex().encode() + b"\n"
        if tamper:
            payload = payload[:-1] + b"!"
        with tarfile.open(path, "w") as archive:
            for member, data in (("signature", record), (name, payload)):
                entry = tarfile.TarInfo(member)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))

    def run_case(self, case):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target, attempt = root / "Target", root / "Attempt"
            version = target / "System/Library/CoreServices/SystemVersion.plist"
            public = target / "System/Library/PrivateFrameworks/SoftwareUpdate.framework/Versions/A/Resources/PublicKey.plist"
            for path, data in ((version, {"ProductBuildVersion": "8R4088"}), (public, {"ApplePkgKeyData": self.key})):
                path.parent.mkdir(parents=True)
                path.write_bytes(plistlib.dumps(data))
            before = {str(path.relative_to(target)): path.read_bytes() for path in target.rglob("*") if path.is_file()}
            attempt.mkdir()
            combo, security = root / "Combo.tar", root / "Security.tar"
            self.outer(combo, "MacOSXUpdCombo10.4.11Intel.pkg.tar")
            if case != "missing":
                self.outer(security, "SecUpd2009-005Intel.pkg.tar", tamper=True)
            if case == "cancel":
                (attempt / "Cancel").touch()
            result = subprocess.run((self.probe, "--tiger-updates", target, attempt, combo, security), capture_output=True)
            after = {str(path.relative_to(target)): path.read_bytes() for path in target.rglob("*") if path.is_file()}
            self.assertEqual(after, before)
            self.assertFalse((attempt / "TigerUpdates").exists())
            if case == "cancel":
                self.assertEqual(result.returncode, 130, result.stderr)
                self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
            else:
                self.assertEqual(result.returncode, 0, result.stderr)
                report = plistlib.loads(result.stdout)
                self.assertEqual(report["Applied"], [])
                self.assertEqual(report["Notice"]["Status"], "original-system-kept")
                if case == "tampered":
                    self.assertIn(b"signature did not verify", result.stderr)
                    self.assertNotIn(b"tar -xpf", result.stderr)

    def test_bad_second_signature_preserves_base_before_any_package_unpack(self):
        self.run_case("tampered")

    def test_large_metadata_map_leaf_cleanup_preserves_unrelated_entries(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run((self.probe, "--cleanup-performance", temporary), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = plistlib.loads(result.stdout)
            self.assertEqual(report["MetadataRemaining"], 200000)
            self.assertEqual(report["FilesRemaining"], 0)

    def test_highsierra_missing_unsigned_and_cancelled_chain_preserves_original(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": "10.13.6", "ProductBuildVersion": "17G66"}))
                before = version.read_bytes()
                attempt.mkdir(); source.mkdir()
                if case == "unsigned":
                    (source / "HopSecurity.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--highsierra-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "HighSierraUpdates").exists())
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plistlib.loads(result.stdout)["Applied"], [])

    def check_apfs_original_preserved(self, guest, build, release, folder):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": release, "ProductBuildVersion": build}))
                before = version.read_bytes(); attempt.mkdir(); source.mkdir()
                if case == "unsigned":
                    (source / "Security.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--" + guest + "-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / folder).exists())
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plistlib.loads(result.stdout)["Applied"], [])

    def test_mojave_missing_unsigned_and_cancelled_chain_preserves_original(self):
        self.check_apfs_original_preserved("mojave", "18G103", "10.14.6", "MojaveUpdates")

    def test_catalina_missing_unsigned_and_cancelled_chain_preserves_original(self):
        self.check_apfs_original_preserved("catalina", "19H15", "10.15.7", "CatalinaUpdates")

    def test_catalina_postflight_preserves_unrelated_files_and_sets_native_directory_permissions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); target, package = root / "Target", root / "Package"
            package.mkdir()
            (package / "cleanListSUConstructed").write_text("./obsolete/content\n")
            (package / "cleanDirectories").write_text("./obsolete\n./nonempty\n")
            for path, data in {
                "obsolete/content": b"old", "nonempty/keep": b"retain",
                "System/Library/PrivateFrameworks/MobileDevice.framework 1/old": b"duplicate",
                "System/Library/PrivateFrameworks/MobileDevice.framework/current": b"retain",
                "usr/standalone/i386/boot.efi": b"updated loader",
                "System/Library/CoreServices/boot.efi": b"old loader",
            }.items():
                file = target / path; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(data)
            for prefix in ("", "System/Library/Templates/Data/"):
                apps = target / (prefix + "Applications"); apps.mkdir(parents=True); apps.chmod(0o755)
                log = target / (prefix + "private/var/log/DiagnosticMessages"); log.mkdir(parents=True); log.chmod(0o777)
            for phase in ("before", "after"):
                result = subprocess.run((self.probe, "--catalina-security-" + phase, target, package), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            report = plistlib.loads(result.stdout)
            self.assertEqual(report["ApplicationsMetadata"], [0, 80, stat.S_IFDIR | 0o775])
            self.assertFalse((target / "obsolete").exists())
            self.assertFalse((target / "System/Library/PrivateFrameworks/MobileDevice.framework 1").exists())
            self.assertEqual((target / "System/Library/PrivateFrameworks/MobileDevice.framework/current").read_bytes(), b"retain")
            self.assertEqual((target / "nonempty/keep").read_bytes(), b"retain")
            self.assertEqual((target / "System/Library/CoreServices/boot.efi").read_bytes(), b"updated loader")
            for prefix in ("", "System/Library/Templates/Data/"):
                self.assertEqual(stat.S_IMODE((target / (prefix + "Applications")).stat().st_mode), 0o775)
                self.assertEqual(stat.S_IMODE((target / (prefix + "private/var/log/DiagnosticMessages")).stat().st_mode), 0o750)

    def test_mojave_native_cleanup_and_postflight_do_not_repair_service_accounts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); target, package = root / "Target", root / "Package"
            package.mkdir()
            (package / "cleanListSUConstructed").write_text("./obsolete/content\n")
            (package / "cleanDirectories").write_text("./obsolete\n./nonempty\n")
            for path, data in {
                "obsolete/content": b"old", "nonempty/keep": b"retain",
                "private/etc/postfix/main.cf": b"custom postfix config",
                "usr/standalone/i386/boot.efi": b"updated genuine loader",
                "System/Library/CoreServices/boot.efi": b"old loader",
            }.items():
                file = target / path; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(data)
            log = target / "private/var/log/DiagnosticMessages"; log.mkdir(parents=True); log.chmod(0o777)
            for phase in ("before", "after"):
                result = subprocess.run((self.probe, "--mojave-security-" + phase, target, package), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((target / "obsolete").exists())
            self.assertEqual((target / "nonempty/keep").read_bytes(), b"retain")
            self.assertEqual((target / "private/etc/postfix/main.cf").read_bytes(), b"custom postfix config")
            self.assertEqual((target / "System/Library/CoreServices/boot.efi").read_bytes(), b"updated genuine loader")
            self.assertEqual(stat.S_IMODE(log.stat().st_mode), 0o750)
            self.assertFalse((target / "private/var/db/dslocal").exists())

    def test_highsierra_native_static_cleanup_accounts_and_postflight(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); target, package = root / "Target", root / "Package"
            tools = package / "ScriptFiles/Tools"; tools.mkdir(parents=True)
            (package / "cleanListSUConstructed").write_text("./obsolete/content\n")
            (package / "cleanDirectories").write_text("./obsolete\n./nonempty\n")
            (tools / "main.cf").write_bytes(b"new stock postfix config")
            for path, data in {
                "obsolete/content": b"old", "nonempty/keep": b"retain",
                "private/etc/postfix/main.cf": b"custom postfix config",
                "usr/standalone/i386/boot.efi": b"updated genuine loader",
                "System/Library/CoreServices/boot.efi": b"old loader",
                "private/var/db/.configureLocalKDC": b"old KDC marker",
            }.items():
                file = target / path; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(data)
            users = target / "private/var/db/dslocal/nodes/Default/users"; users.mkdir(parents=True)
            original = {"name": ["mac"], "uid": ["501"], "ShadowHashData": [b"preserve this credential"]}
            (users / "mac.plist").write_bytes(plistlib.dumps(original))
            pristine = target / "System/Library/DirectoryServices/DefaultLocalDB/Default/users"; pristine.mkdir(parents=True)
            (pristine / "_carddav.plist").write_bytes(plistlib.dumps({"name": ["_carddav"], "ShadowHashData": [b"old"]}))
            log = target / "private/var/log/DiagnosticMessages"; log.mkdir(parents=True); log.chmod(0o777)
            first = subprocess.run((self.probe, "--highsierra-security-before", target, package), capture_output=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertFalse((target / "obsolete/content").exists())
            users.chmod(0o500)
            result = subprocess.run((self.probe, "--highsierra-security-after", target, package), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(plistlib.loads(result.stdout)["UserDirectoryMode"], 0o500)
            self.assertFalse((target / "obsolete").exists())
            self.assertEqual((target / "nonempty/keep").read_bytes(), b"retain")
            self.assertEqual((target / "private/etc/postfix/main.cf").read_bytes(), b"custom postfix config")
            self.assertEqual((target / "System/Library/CoreServices/boot.efi").read_bytes(), b"updated genuine loader")
            self.assertEqual(plistlib.loads((users / "mac.plist").read_bytes()), original)
            service = plistlib.loads((users / "_carddav.plist").read_bytes())
            self.assertEqual(service["IsHidden"], ["YES"]); self.assertNotIn("ShadowHashData", service)
            self.assertFalse((target / "private/var/db/.configureLocalKDC").exists())
            for marker in (".AppleODConvertDone", ".AppleODLegacyDisable"):
                self.assertTrue((target / "private/var/db/SystemPolicyConfiguration" / marker).exists())
            self.assertEqual(stat.S_IMODE(log.stat().st_mode), 0o750)
            self.assertTrue((target / "usr/local/.com.apple.installer.keep").exists())

    def test_update_receipts_replace_protected_original_and_pair_script_only_bom(self):
        for protected, has_payload in ((False, False), (False, True), (True, True)):
            with self.subTest(protected=protected, has_payload=has_payload), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); target = root / "Target"; target.mkdir()
                identifier = "com.apple.pkg.ReceiptProbe"
                folder = "System/Library/Receipts" if protected else "private/var/db/receipts"
                base = target / folder / identifier
                receipt, bom = Path(str(base) + ".plist"), Path(str(base) + ".bom")
                if protected:
                    base.parent.mkdir(parents=True)
                    receipt.write_bytes(plistlib.dumps({"PackageVersion": "old"}))
                    base.parent.chmod(0o555)
                package = {"Identifier": identifier, "Name": "ReceiptProbe", "Version": "new", "Groups": []}
                if has_payload:
                    source = root / "Source.bom"
                    subprocess.run(("/usr/bin/mkbom", "-i", "/dev/null", source), check=True)
                    package["BOM"] = str(source)
                record = root / "Package.plist"; record.write_bytes(plistlib.dumps(package))
                result = subprocess.run((self.probe, "--write-update-receipt", target, record), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(plistlib.loads(receipt.read_bytes())["PackageVersion"], "new")
                entries = subprocess.check_output(("/usr/bin/lsbom", bom))
                self.assertEqual(entries, b"")
                if protected:
                    self.assertFalse((target / "private/var/db/receipts").exists())

    def test_unavailable_second_package_keeps_original_system(self):
        self.run_case("missing")

    def test_cancellation_is_not_downgraded_to_optional_update_failure(self):
        self.run_case("cancel")

    def test_leopard_missing_unsigned_and_cancelled_updates_never_mutate_base(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt = root / "Target", root / "Attempt"
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductBuildVersion": "9J3050"}))
                before = version.read_bytes()
                attempt.mkdir()
                combo, security = root / "Combo.pkg", root / "Security.pkg"
                if case != "missing":
                    # Not signed Apple Software; never even attempt extraction.
                    combo.write_bytes(b"not an authentic update")
                    security.write_bytes(b"not an authentic update")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--leopard-updates", target, attempt, combo, security),
                                        capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(list(target.rglob("*")), [target / "System", target / "System/Library",
                                                          target / "System/Library/CoreServices", version])
                self.assertFalse((attempt / "LeopardUpdates").exists())
                self.assertNotIn(b"xar -", result.stderr)
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    report = plistlib.loads(result.stdout)
                    self.assertEqual(report["Applied"], [])
                    self.assertEqual(report["Notice"]["Status"], "original-system-kept")

    def test_snow_multipart_failure_preserves_base_and_cancel_is_not_a_fallback(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductBuildVersion": "10J3250"}))
                before = version.read_bytes()
                attempt.mkdir()
                source.mkdir()
                if case == "unsigned":
                    (source / "MacOSXUpdCombo10.6.8.Part0.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--snow-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "SnowLeopardUpdates").exists())
                self.assertNotIn(b"xar -", result.stderr)
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    report = plistlib.loads(result.stdout)
                    self.assertEqual(report["Applied"], [])
                    self.assertEqual(report["Notice"]["Status"], "original-system-kept")

    def test_lion_missing_unsigned_and_cancelled_security_update_preserves_base(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": "10.7.5", "ProductBuildVersion": "11G63"}))
                before = version.read_bytes()
                attempt.mkdir()
                source.mkdir()
                if case == "unsigned":
                    (source / "SecUpd2014-004Lion.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--lion-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "LionUpdates").exists())
                self.assertNotIn(b"xar -", result.stderr)
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plistlib.loads(result.stdout)["Notice"]["Status"], "original-system-kept")

    def test_mountainlion_missing_unsigned_and_cancelled_security_update_preserves_base(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": "10.8.5", "ProductBuildVersion": "12F45"}))
                before = version.read_bytes()
                attempt.mkdir()
                source.mkdir()
                if case == "unsigned":
                    (source / "SecUpd2015-006MtLion.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--mountainlion-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "MountainLionUpdates").exists())
                self.assertNotIn(b"xar -", result.stderr)
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plistlib.loads(result.stdout)["Notice"]["Status"], "original-system-kept")

    def test_mavericks_missing_unsigned_and_cancelled_security_update_preserves_base(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": "10.9.5", "ProductBuildVersion": "13F34"}))
                before = version.read_bytes()
                attempt.mkdir()
                source.mkdir()
                if case == "unsigned":
                    (source / "SecUpd2016-004Mavericks.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--mavericks-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "MavericksUpdates").exists())
                self.assertNotIn(b"xar -", result.stderr)
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plistlib.loads(result.stdout)["Notice"]["Status"], "original-system-kept")

    def test_mavericks_postflight_preserves_installer_media_and_removes_only_native_obsolete_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target, package = root / "Target", root / "Package"
            tools = package / "ScriptFiles/Tools"
            tools.mkdir(parents=True)
            (tools / "cleanListSUConstructed").write_text("./obsolete.txt\n./nonempty\n")
            assistant = target / "Install OS X Mavericks.app/Contents"
            paths = ["_CodeSignature/CodeResources", "Info.plist", "MacOS/Install OS X Mavericks", "version.plist",
                     "Resources/createinstallmedia", "Resources/InstallAssistantTool"]
            for bundle, binary in (("DM_10_6.IABundle", "DM_10_6"), ("IA.bundle", "IA"),
                                   ("IACoreStorage.IABundle", "IACoreStorage")):
                paths += [f"PlugIns/{bundle}/Contents/{leaf}" for leaf in
                          ("_CodeSignature/CodeResources", "Info.plist", "version.plist", f"MacOS/{binary}")]
            paths += ["PlugIns/IA.bundle/Contents/MacOS/libBaseIA.dylib"]
            paths += [f"Resources/FindReaperFiles.app/Contents/{leaf}" for leaf in
                      ("_CodeSignature/CodeResources", "MacOS/FindReaperFiles", "version.plist")]
            for path in paths:
                file = assistant / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(b"obsolete installer binary")
            media = assistant / "SharedSupport/InstallESD.dmg"
            media.parent.mkdir()
            media.write_bytes(b"user installer media")
            plugin = target / "Library/Internet Plug-Ins/QuickTime Plugin.plugin/Contents/MacOS/QuickTime Plugin"
            plugin.parent.mkdir(parents=True)
            plugin.write_bytes(b"obsolete plugin")
            (target / "obsolete.txt").write_bytes(b"obsolete")
            (target / "nonempty").mkdir()
            (target / "nonempty/keep").write_bytes(b"unrelated")
            kernel = target / "mach_kernel"
            kernel.write_bytes(b"new kernel")
            result = subprocess.run((self.probe, "--mavericks-security-actions", target, package), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(media.read_bytes(), b"user installer media")
            self.assertFalse((assistant / "PlugIns").exists())
            self.assertFalse((assistant / "Resources").exists())
            self.assertFalse((plugin.parents[2]).exists())
            self.assertFalse((target / "obsolete.txt").exists())
            self.assertEqual((target / "nonempty/keep").read_bytes(), b"unrelated")
            self.assertTrue(kernel.stat().st_flags & stat.UF_HIDDEN)
            self.assertTrue(all(not (assistant / path).exists() for path in paths))

    def test_yosemite_missing_unsigned_and_cancelled_security_update_preserves_base(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": "10.10.5", "ProductBuildVersion": "14F27"}))
                before = version.read_bytes()
                attempt.mkdir()
                source.mkdir()
                if case == "unsigned":
                    (source / "SecUpd2017-003Yosemite.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--yosemite-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "YosemiteUpdates").exists())
                self.assertNotIn(b"xar -", result.stderr)
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plistlib.loads(result.stdout)["Notice"]["Status"], "original-system-kept")

    def test_yosemite_native_pre_postflight_order_and_conditional_photos_preservation(self):
        for has_photos in (True, False):
            with self.subTest(has_photos=has_photos), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, package = root / "Target", root / "Package"
                tools = package / "ScriptFiles/Tools"
                tools.mkdir(parents=True)
                (tools / "cleanListSUConstructed").write_text("./Services/Example.xpc/Contents/obsolete\n./obsolete/tree\n")
                paths = ["Services/Example.xpc/Contents/obsolete", "obsolete/tree/file", "private/var/db/BootCache.playlist",
                         "private/var/db/spindump/UUIDToBinaryLocations", "usr/local/sbin/launchd.development",
                         "System/Library/Kernels/kernel.development",
                         "System/Library/Caches/com.apple.kext.caches/Startup/kernelcache.development",
                         "System/Library/PrelinkedKernels/prelinkedkernel.development",
                         "Library/Internet Plug-Ins/QuickTime Plugin.plugin/Contents/old",
                         "Payloads/External/System/Library/CoreServices/FirmwareUpdates/old",
                         "System/Library/PrivateFrameworks/CloudPhotoLibrary.framework/old",
                         "System/Library/PrivateFrameworks/PhotoLibraryPrivate.framework/old"]
                for framework in ("CloudPhotoServices.framework", "PhotoLibrary.framework"):
                    paths += [f"System/Library/PrivateFrameworks/{framework}/{leaf}" for leaf in
                              ("Versions/A/old", "Versions/A/Frameworks/Nested/keep", "old")]
                for path in paths:
                    file = target / path
                    file.parent.mkdir(parents=True, exist_ok=True)
                    file.write_bytes(b"original")
                if has_photos:
                    (target / "Applications/Photos.app").mkdir(parents=True)
                itunes = target / "Applications/iTunes.app"
                itunes.mkdir(parents=True)
                subprocess.run(("/usr/bin/xattr", "-w", "com.apple.writer_bundle_identifier", "obsolete", itunes), check=True)
                subprocess.run(("/usr/bin/xattr", "-w", "user.fixture", "keep", itunes), check=True)
                original_boot = target / "usr/standalone/i386/boot.efi"
                original_boot.parent.mkdir(parents=True)
                original_boot.write_bytes(b"installed boot loader")
                boot = target / "System/Library/CoreServices/boot.efi"
                boot.parent.mkdir(parents=True)
                boot.write_bytes(b"stale")
                pre = subprocess.run((self.probe, "--yosemite-security-before", target, package), capture_output=True)
                self.assertEqual(pre.returncode, 0, pre.stderr)
                xpc = target / paths[0]
                self.assertFalse(xpc.exists())
                self.assertTrue((target / "obsolete/tree/file").exists())
                self.assertTrue(all(not (target / path).exists() for path in paths if path.endswith(".development")))
                xpc.write_bytes(b"replacement service must survive postflight")
                post = subprocess.run((self.probe, "--yosemite-security-after", target, package), capture_output=True)
                self.assertEqual(post.returncode, 0, post.stderr)
                self.assertEqual(xpc.read_bytes(), b"replacement service must survive postflight")
                self.assertFalse((target / "obsolete/tree").exists())
                self.assertFalse((target / "Payloads").exists())
                self.assertFalse((target / "Library/Internet Plug-Ins/QuickTime Plugin.plugin").exists())
                self.assertFalse((target / "private/var/db/BootCache.playlist").exists())
                self.assertFalse((target / "private/var/db/spindump/UUIDToBinaryLocations").exists())
                self.assertNotIn(b"com.apple.writer_bundle_identifier", subprocess.check_output(("/usr/bin/xattr", itunes)))
                self.assertEqual(subprocess.check_output(("/usr/bin/xattr", "-p", "user.fixture", itunes)).strip(), b"keep")
                self.assertEqual(boot.read_bytes(), original_boot.read_bytes())
                for framework in ("CloudPhotoServices.framework", "PhotoLibrary.framework"):
                    folder = target / "System/Library/PrivateFrameworks" / framework
                    self.assertEqual((folder / "Versions/A/old").exists(), has_photos)
                    self.assertTrue((folder / "Versions/A/Frameworks/Nested/keep").exists())
                self.assertEqual((target / "System/Library/PrivateFrameworks/CloudPhotoLibrary.framework").exists(), has_photos)

    def test_sierra_missing_unsigned_and_cancelled_security_chain_preserves_base(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": "10.12.6", "ProductBuildVersion": "16G29"}))
                original = version.read_bytes()
                attempt.mkdir()
                source.mkdir()
                if case == "unsigned":
                    for name in ("SecUpd2019-005Sierra.pkg", "FullBundleUpdate.pkg"):
                        (source / name).write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--sierra-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), original)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "SierraUpdates").exists())
                self.assertEqual(result.returncode, 130 if case == "cancel" else 0, result.stderr)
                if case != "cancel":
                    report = plistlib.loads(result.stdout)
                    self.assertEqual(report["Applied"], [])
                    self.assertEqual(report["Notice"]["Status"], "original-system-kept")

    def test_sierra_removes_kext_contents_before_payload_and_only_empty_directories_after(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target, package = root / "Target", root / "Package"
            tools = package / "ScriptFiles/Tools"
            tools.mkdir(parents=True)
            (tools / "cleanListSUConstructed").write_text(
                "./Services/Test.xpc/Contents/old\n./Drivers/Test.kext/Contents/old\n./obsolete\n./Alias/keep\n")
            (tools / "cleanDirectories").write_text("./empty\n./nonempty\n./DirectoryAlias/empty\n")
            for path in ("Services/Test.xpc/Contents/old", "Drivers/Test.kext/Contents/old", "obsolete",
                         "Real/keep", "nonempty/keep", "RealDirectory/empty/keep",
                         "private/var/db/dyld/keep", "System/Library/Extensions/keep", "Library/Extensions/keep",
                         "usr/standalone/i386/boot.efi", "System/Library/CoreServices/boot.efi"):
                file = target / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(path.encode())
            (target / "empty").mkdir()
            (target / "Alias").symlink_to("Real")
            (target / "DirectoryAlias").symlink_to("RealDirectory")
            result = subprocess.run((self.probe, "--sierra-security-before", target, package), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for path in ("Services/Test.xpc/Contents/old", "Drivers/Test.kext/Contents/old"):
                self.assertFalse((target / path).exists())
                (target / path).write_bytes(b"new payload")
            self.assertTrue((target / "obsolete").exists())
            result = subprocess.run((self.probe, "--sierra-security-after", target, package), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((target / "obsolete").exists())
            self.assertFalse((target / "empty").exists())
            for path in ("Real/keep", "nonempty/keep", "RealDirectory/empty/keep"):
                self.assertTrue((target / path).exists())
            for path in ("Services/Test.xpc/Contents/old", "Drivers/Test.kext/Contents/old"):
                self.assertEqual((target / path).read_bytes(), b"new payload")
            report = plistlib.loads(result.stdout)
            self.assertEqual(report["CatalogFlags"], [0x10] * 4)
            self.assertNotIn("obsolete", report["MetadataPaths"])
            self.assertNotIn("empty", report["MetadataPaths"])
            self.assertIn("nonempty/keep", report["MetadataPaths"])
            self.assertIn("Real/keep", report["MetadataPaths"])
            self.assertEqual((target / "System/Library/CoreServices/boot.efi").read_bytes(),
                             b"usr/standalone/i386/boot.efi")

    def test_elcapitan_missing_unsigned_and_cancelled_security_chain_preserves_base(self):
        for case in ("missing", "unsigned", "cancel"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, attempt, source = (root / name for name in ("Target", "Attempt", "Inputs"))
                version = target / "System/Library/CoreServices/SystemVersion.plist"
                version.parent.mkdir(parents=True)
                version.write_bytes(plistlib.dumps({"ProductVersion": "10.11.6", "ProductBuildVersion": "15G31"}))
                before = version.read_bytes()
                attempt.mkdir()
                source.mkdir()
                if case == "unsigned":
                    (source / "SecUpd2018-004ElCapitan.pkg").write_bytes(b"not an Apple package")
                    (source / "FullBundleUpdate.pkg").write_bytes(b"not an Apple package")
                if case == "cancel":
                    (attempt / "Cancel").touch()
                result = subprocess.run((self.probe, "--elcapitan-updates", target, attempt, source), capture_output=True)
                self.assertEqual(version.read_bytes(), before)
                self.assertEqual(len(list(target.rglob("*"))), 4)
                self.assertFalse((attempt / "ElCapitanUpdates").exists())
                self.assertNotIn(b"xar -", result.stderr)
                if case == "cancel":
                    self.assertEqual(result.returncode, 130, result.stderr)
                    self.assertFalse((attempt / "HostUpdateNotice.plist").exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plistlib.loads(result.stdout)["Notice"]["Status"], "original-system-kept")

    def test_elcapitan_cleanup_skips_aliased_parents_and_stages_privileged_flags_offline(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            target, package = root / "Target", root / "Package"
            tools = package / "ScriptFiles/Tools"
            tools.mkdir(parents=True)
            (tools / "cleanListSUConstructed").write_text("./Alias/keep\n./obsolete\n./Services/Example.xpc/Contents/old\n")
            paths = ["Real/keep", "obsolete", "Services/Example.xpc/Contents/old",
                     "private/var/db/dyld/keep", "private/var/db/BootCache.playlist",
                     "private/var/db/spindump/UUIDToBinaryLocations",
                     "System/Library/Extensions/keep", "Library/Extensions/keep",
                     "usr/standalone/i386/boot.efi", "System/Library/CoreServices/boot.efi"]
            for path in paths:
                file = target / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(path.encode())
            (target / "Alias").symlink_to("Real")
            pre = subprocess.run((self.probe, "--elcapitan-security-before", target, package), capture_output=True)
            self.assertEqual(pre.returncode, 0, pre.stderr)
            xpc = target / "Services/Example.xpc/Contents/old"
            self.assertFalse(xpc.exists())
            xpc.write_bytes(b"new service")
            post = subprocess.run((self.probe, "--elcapitan-security-after", target, package), capture_output=True)
            self.assertEqual(post.returncode, 0, post.stderr)
            report = plistlib.loads(post.stdout)
            protected = ("private", "private/var", "private/var/db", "private/var/db/dyld")
            self.assertEqual(report["SystemFlags"], {path: 0x10 for path in protected})
            self.assertEqual(report["CatalogFlags"], [0x10] * 4)
            self.assertTrue(all(not (target / path).stat().st_flags & stat.SF_NOUNLINK for path in protected))
            self.assertEqual((target / "Real/keep").read_bytes(), b"Real/keep")
            self.assertFalse((target / "obsolete").exists())
            self.assertEqual(xpc.read_bytes(), b"new service")
            self.assertFalse((target / "private/var/db/BootCache.playlist").exists())
            self.assertFalse((target / "private/var/db/spindump/UUIDToBinaryLocations").exists())
            self.assertEqual((target / "System/Library/CoreServices/boot.efi").read_bytes(), b"usr/standalone/i386/boot.efi")

    def test_mountainlion_postflight_updates_only_active_guest_policy_and_hides_kernel(self):
        for active, reset in ((False, True), (True, True), (True, False)):
            with self.subTest(active=active, reset=reset), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                target, package = root / "Target", root / "Package"
                database = target / "private/var/db/SystemPolicy"
                database.parent.mkdir(parents=True)
                package.mkdir()
                default = database.with_name(".SystemPolicy-default")
                default.write_bytes(b"original default policy remains untouched")
                prefs = database.with_name("SystemPolicy-prefs.plist")
                prefs.write_bytes(plistlib.dumps({"enabled": "no" if reset else "yes", "Other": "kept"}))
                kernel = target / "mach_kernel"
                kernel.write_bytes(b"updated guest kernel")
                if active:
                    with sqlite3.connect(database) as db:
                        db.executescript("CREATE TABLE object(value TEXT); INSERT INTO object VALUES('cached');"
                                         "CREATE TABLE authority(label TEXT,disabled INT);"
                                         "INSERT INTO authority VALUES('Developer ID',1); INSERT INTO authority VALUES('Other',1);")
                    database.chmod(0o640)
                result = subprocess.run((self.probe, "--mountainlion-security-actions", target, package), capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(default.read_bytes(), b"original default policy remains untouched")
                self.assertEqual(plistlib.loads(prefs.read_bytes()), {"enabled": "yes", "Other": "kept"})
                self.assertEqual(kernel.read_bytes(), b"updated guest kernel")
                self.assertTrue(kernel.stat().st_flags & stat.UF_HIDDEN)
                if active:
                    self.assertEqual(stat.S_IMODE(database.stat().st_mode), 0o640)
                    with sqlite3.connect(database) as db:
                        self.assertEqual(db.execute("SELECT count(*) FROM object").fetchone()[0], 0)
                        self.assertEqual(dict(db.execute("SELECT label,disabled FROM authority")), {"Developer ID": 0 if reset else 1, "Other": 1})
                else:
                    self.assertFalse(database.exists())
                self.assertEqual(list(package.iterdir()), [])
                self.assertFalse(any(p.name.endswith(("-journal", "-wal", "-shm")) for p in target.rglob("*")))

    def test_lion_native_postflight_removes_plugin_bundles_and_moves_apache_module(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target, package = root / "Target", root / "Package"
            obsolete = target / "Library/Application Support/Apple/BezelServices/AppleHIDMouse.plugin"
            (obsolete / "Contents").mkdir(parents=True)
            (obsolete / "Contents/obsolete").write_bytes(b"old plugin")
            cleanup = package / "ScriptFiles/Tools/cleanList"
            cleanup.parent.mkdir(parents=True)
            cleanup.write_text("/Library/Application Support/Apple/BezelServices/AppleHIDMouse.plugin\n")
            restart = target / "System/Library/CoreServices/loginwindow.app/Contents/Resources/Restart.tif"
            restart.parent.mkdir(parents=True)
            restart.write_bytes(b"obsolete resource")
            config = target / "private/etc/apache2/httpd.conf"
            config.parent.mkdir(parents=True)
            original = "# Apple specific modules\n<IfDefine MACOSXSERVER>\nLoadModule hfs_apple_module libexec/apache2/mod_hfs_apple.so\n</IfDefine>\n"
            config.write_text(original)
            module = target / "usr/libexec/apache2/mod_hfs_apple.so"
            module.parent.mkdir(parents=True)
            module.write_bytes(b"bundled module")
            jobs = ("LaunchDaemons/com.apple.mrt.plist", "LaunchAgents/com.apple.mrt.uiagent.plist")
            for name in jobs:
                job = target / "System/Library" / name
                job.parent.mkdir(parents=True, exist_ok=True)
                job.write_bytes(plistlib.dumps({"Disabled": True, "Label": name}))
            subprocess.run((self.probe, "--lion-security-actions", target, package), check=True, capture_output=True)
            self.assertFalse(obsolete.exists())
            self.assertFalse(restart.exists())
            self.assertEqual(config.with_name("httpd.conf.pre-update").read_text(), original)
            self.assertEqual(config.read_text().count("LoadModule hfs_apple_module"), 1)
            self.assertTrue(config.read_text().startswith("# Apple specific modules\nLoadModule hfs_apple_module"))
            for name in jobs:
                self.assertFalse(plistlib.loads((target / "System/Library" / name).read_bytes())["Disabled"])


if __name__ == "__main__":
    unittest.main()
