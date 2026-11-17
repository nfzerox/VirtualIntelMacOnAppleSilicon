import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile


TOOLS = Path(__file__).resolve().parents[1] / "tools"
PROBE = r'''#import "VIMBigSurFixups.h"
#import "VIMBigSurArchive.h"
#include <AppleArchive/AppleArchive.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/xattr.h>
#include <dlfcn.h>
// SIP controls creation of native labels even in temporary fixtures. Substitute
// only that attribute's name in this test executable; the production reader,
// reserved-slot mutation and filesystem error handling still run unchanged.
static BOOL fixtureNativeLabels;
ssize_t getxattr(const char *path,const char *name,void *value,size_t size,uint32_t position,int options) {
    static ssize_t (*original)(const char *,const char *,void *,size_t,uint32_t,int);
    if(!original)original=dlsym(RTLD_NEXT,"getxattr");
    if(fixtureNativeLabels&&!strcmp(name,"com.apple.rootless"))name="com.virtualintelmac.test.native-rootless";
    return original(path,name,value,size,position,options);
}
static int ProbeAttributes(void *opaque,AAEntryMessage message,__unused const char *path,void *data) {
    if(message==AA_ENTRY_MESSAGE_EXTRACT_ATTRIBUTES&&data) {
        AAEntryAttributes *attributes=data;
        attributes->UID=attributes->GID=attributes->FLG=0;
        attributes->bits&=*(uint32_t *)opaque;
    }
    return message==AA_ENTRY_MESSAGE_EXTRACT_FAIL?-1:0;
}
int main(int argc, char **argv) {@autoreleasepool {
    if(argc==4&&(!strcmp(argv[3],"--data-protection")||!strcmp(argv[3],"--data-protection-fixture"))) {
        fixtureNativeLabels=!strcmp(argv[3],"--data-protection-fixture");
        NSError *error=nil;
        BOOL ok=VIMBigSurStageDataProtection([NSURL fileURLWithPath:@(argv[2])],
            [NSURL fileURLWithPath:@(argv[1])],&error);
        if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
        return ok?0:1;
    }
    if(argc==5&&!strcmp(argv[4],"--raw-attributes")) {
        uint32_t mask=(uint32_t)strtoul(argv[3],NULL,0);
        AAByteStream file=AAFileStreamOpenWithPath(argv[1],O_RDONLY,0);
        AAArchiveStream input=file?AADecodeArchiveInputStreamOpen(file,NULL,NULL,0,1):NULL;
        AAArchiveStream output=input?AAExtractArchiveOutputStreamOpen(argv[2],&mask,ProbeAttributes,0,1):NULL;
        BOOL ok=output&&AAArchiveStreamProcess(input,output,NULL,NULL,0,1)>=0;
        if(output&&AAArchiveStreamClose(output))ok=NO;
        if(input&&AAArchiveStreamClose(input))ok=NO;
        if(file&&AAByteStreamClose(file))ok=NO;
        return ok?0:1;
    }
    if(argc==4&&!strcmp(argv[3],"--extract")) {
        NSError *error=nil;
        BOOL ok=VIMBigSurExtractPayloadMember([NSURL fileURLWithPath:@(argv[1])],
            @"AssetData/payloadv2/payload.000",[NSURL fileURLWithPath:@(argv[2])],NULL,NULL,&error);
        if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
        return ok?0:1;
    }
    if(argc==3&&!strcmp(argv[2],"--policy")) {
        NSError *error=nil;
        NSDictionary *policy=VIMBigSurReadSealPolicy([NSURL fileURLWithPath:@(argv[1])],&error);
        if(!policy){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
        NSDictionary *links=policy[@"AlternateLinks"];
        for(NSString *path in links) {
            NSData *value=links[path];
            printf("%s: %.*s\n",path.UTF8String,(int)value.length,(const char *)value.bytes);
        }
        return 0;
    }
    if(argc==4&&!strcmp(argv[3],"--compress")) {
        NSError *error=nil;
        BOOL ok=VIMBigSurRestoreCompression([NSURL fileURLWithPath:@(argv[2])],
            [NSURL fileURLWithPath:@(argv[1])],nil,nil,&error);
        if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
        return ok?0:1;
    }
    if(argc==4||(argc==5&&!strcmp(argv[4],"--detach"))) {
        int root=open(argv[1],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        BOOL linked=root>=0&&(argc==5?VIMBigSurDetachFixupHardLink(root,@(argv[2]),@(argv[3])):
            VIMBigSurStageFixupHardLink(root,@(argv[2]),@(argv[3])));
        if(root>=0)close(root);
        return linked?0:1;
    }
    if(argc!=2)return 2;
    NSURL *zip=[NSURL fileURLWithPath:@(argv[1])];
    NSError *error=nil;
    __block size_t rows=0;
    BOOL ok=VIMBigSurVisitFixups(zip,^BOOL(NSString *path,VIMBigSurFixup row,NSString *owner){
        if([path isEqual:@"owner"]||[path isEqual:@"alias"])
            printf("%s %u %u %o %u %u %llu %s\n",path.UTF8String,row.uid,row.gid,row.mode,row.flags,
                row.compressionType,(unsigned long long)row.resourceLength,owner.UTF8String?:"-");
        rows++;
        return YES;
    },NULL,NULL,&error);
    if(!ok){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    printf("%zu\n",rows);
    return 0;
}}
'''


def row(name, fields):
    path = name.encode()
    header = b"TYP1FPATP" + struct.pack("<H", len(path)) + path + fields
    return b"YAA1" + struct.pack("<H", 6 + len(header)) + header


@unittest.skipUnless(sys.platform == "darwin", "Uses Apple's native archive reader")
class NativeFixupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(folder.cleanup)
        cls.root = Path(folder.name)
        cls.probe = cls.root / "probe"
        source = cls.root / "probe.m"
        source.write_text(PROBE)
        subprocess.run(("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                        "-I", TOOLS / "HostInstall", "-I", TOOLS.parent / "vendor/xz-api",
                        source, TOOLS / "HostInstall/VIMBigSurFixups.m",
                        TOOLS / "HostInstall/VIMBigSurArchive.m", TOOLS / "HostInstall/VIMBigSurBootAssets.m",
                        TOOLS / "HostInstall/VIMBigSurPayloadExtraction.m",
                        TOOLS / "host-install-pbzx.c",
                        "-framework", "Foundation", "-lAppleArchive", "-larchive",
                        "-llzma.5", "-lcompression", "-o", cls.probe), check=True)

    def probe_rows(self, payload):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            zip_path = Path(folder) / "asset.zip"
            with zipfile.ZipFile(zip_path, "w") as archive:
                prefix = b"pbzx" + struct.pack(">QQQ", len(payload), len(payload), len(payload))
                archive.writestr("AssetData/payloadv2/fixup.manifest", prefix + payload)
            return subprocess.run((self.probe, zip_path), capture_output=True, text=True)

    def test_alias_inherits_signed_inode_metadata_and_compression(self):
        metadata = b"UID1\x00GID1PMOD2\xa4\x01FLG4\x00\x00\x08\x00AFT1\x08AFR2\x34\x12"
        owner = row("owner", metadata + b"HLC1\x03HLO1\x00")
        alias = row("alias", b"HLC1\x03HLO1\x01")
        reply = self.probe_rows(owner + alias)
        self.assertEqual(reply.returncode, 0, reply.stderr)
        self.assertEqual(reply.stdout.splitlines(), [
            "owner 0 80 100644 524288 8 4660 -", "alias 0 80 100644 524288 8 4660 owner", "2"])
        self.assertNotEqual(self.probe_rows(alias).returncode, 0)
        self.assertNotEqual(self.probe_rows(owner + row("alias", b"HLC1\x03HLO1\x01UID1\x01")).returncode, 0)

    def test_restores_missing_alias_but_never_overwrites_or_follows_symlinks(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            root = Path(folder) / "system"
            root.mkdir()
            parent = root / "bin"
            parent.mkdir()
            owner = parent / "at"
            owner.write_bytes(b"original")
            owner.chmod(0o4555)
            alias = parent / "atq"
            parent.chmod(0o555)
            stamp = parent.stat().st_mtime_ns
            try:
                for _ in range(2):
                    subprocess.run((self.probe, root, "bin/at", "bin/atq"), check=True)
                self.assertEqual(alias.stat().st_ino, owner.stat().st_ino)
                self.assertEqual(alias.stat().st_mode & 0o7777, 0o4555)
                self.assertEqual(parent.stat().st_mode & 0o777, 0o555)
                self.assertEqual(parent.stat().st_mtime_ns, stamp)
                for _ in range(2):
                    subprocess.run((self.probe, root, "bin/at", "bin/atq", "--detach"), check=True)
                self.assertFalse(alias.exists())
                self.assertEqual(owner.read_bytes(), b"original")
                self.assertEqual(parent.stat().st_mode & 0o777, 0o555)
                self.assertEqual(parent.stat().st_mtime_ns, stamp)
            finally:
                parent.chmod(0o755)
            foreign = Path(folder) / "foreign"
            foreign.mkdir()
            (root / "escape").symlink_to(foreign, target_is_directory=True)
            blocked = root / "blocked"
            blocked.write_bytes(b"do not replace")
            for path in ("escape/new", "../new", "blocked"):
                self.assertNotEqual(subprocess.run((self.probe, root, "bin/at", path)).returncode, 0)
                self.assertNotEqual(subprocess.run((self.probe, root, "bin/at", path, "--detach")).returncode, 0)
            self.assertEqual(blocked.read_bytes(), b"do not replace")
            self.assertFalse(list(foreign.iterdir()))

    def test_bundle_resource_can_be_shared_without_changing_bundle_layout(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            root = Path(folder)
            apps = root / "System" / "Applications"
            signed = apps / "App Store.app" / "Contents" / "_CodeSignature"
            signed.mkdir(parents=True)
            owner = signed / "CodeResources"
            owner.write_bytes(b"Apple bundle resource")
            os.link(owner, signed / "existing")
            frameworks = root / "System" / "Library"
            frameworks.mkdir()
            before = apps.stat().st_mtime_ns
            subprocess.run((self.probe, root, str(owner.relative_to(root)),
                            "System/Library/CodeResources"), check=True)
            self.assertEqual({p.stat().st_ino for p in (owner, signed / "existing",
                             frameworks / "CodeResources")}, {owner.stat().st_ino})
            self.assertEqual(apps.stat().st_mtime_ns, before)
            self.assertEqual([p.name for p in apps.iterdir()], ["App Store.app"])

    def test_compression_stage_restores_alias_after_canonical_file(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            root = Path(folder) / "system"
            root.mkdir()
            original = root / "owner"
            original.write_bytes(b"original data")
            os.link(original, root / "alias")
            metadata = b"UID1\x00GID1\x00MOD2\xa4\x01FLG4\x00\x00\x00\x00"
            payload = row("owner", metadata + b"HLC1\x03HLO1\x00") + row("alias", b"HLC1\x03HLO1\x01")
            archive = Path(folder) / "asset.zip"
            with zipfile.ZipFile(archive, "w") as writer:
                writer.writestr("AssetData/payloadv2/fixup.manifest",
                                b"pbzx" + struct.pack(">QQQ", len(payload), len(payload), len(payload)) + payload)
            reply = subprocess.run((self.probe, root, archive, "--compress"), capture_output=True, text=True)
            self.assertEqual(reply.returncode, 0, reply.stderr)
            self.assertEqual(original.stat().st_ino, (root / "alias").stat().st_ino)
            self.assertEqual(original.read_bytes(), b"original data")

    def test_cryptex_altlink_is_deferred_but_original_policy_and_other_xattrs_survive(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            root = Path(folder) / "system"
            parent = root / "System/Cryptexes"
            parent.mkdir(parents=True)
            name = b"System/Cryptexes/OS"
            target = b"../../System/Volumes/Preboot/Cryptexes/OS"
            alternate = b"/System/Volumes/Preboot/Cryptexes/Incoming/OS"
            xattrs = b""
            for key, value in ((b"com.apple.fs.altlink", alternate), (b"org.virtualintelmac.fixture", b"kept")):
                contents = key + b"\0" + value
                xattrs += struct.pack("<I", len(contents) + 4) + contents
            header = (b"TYP1LPATP" + struct.pack("<H", len(name)) + name +
                      b"LNKP" + struct.pack("<H", len(target)) + target +
                      b"UID1\0GID1\0MOD2\xff\x01FLG4\0\0\x08\0" +
                      b"XATA" + struct.pack("<H", len(xattrs)))
            entry = b"YAA1" + struct.pack("<H", len(header) + 6) + header + xattrs
            archive = Path(folder) / "asset.zip"
            def packed(data):
                return b"pbzx" + struct.pack(">QQQ", len(data), len(data), len(data)) + data
            with zipfile.ZipFile(archive, "w") as writer:
                writer.writestr("AssetData/payloadv2/payload.000", packed(entry))
                writer.writestr("AssetData/payloadv2/fixup.manifest", packed(entry))
                writer.writestr("AssetData/payloadv2/firmlinks_payload",
                                row(".", b"UID1\0GID1\0MOD2\xed\x01FLG1\0"))
            reply = subprocess.run((self.probe, archive, root, "--extract"), capture_output=True, text=True)
            self.assertEqual(reply.returncode, 0, reply.stderr)
            link = parent / "OS"
            self.assertEqual(os.readlink(link), target.decode())
            self.assertEqual(subprocess.check_output(("/usr/bin/xattr", "-sp", "org.virtualintelmac.fixture", link)).strip(), b"kept")
            self.assertNotIn(b"com.apple.fs.altlink", subprocess.check_output(("/usr/bin/xattr", "-s", link)).splitlines())
            policy = subprocess.run((self.probe, archive, "--policy"), capture_output=True, text=True)
            self.assertEqual(policy.returncode, 0, policy.stderr)
            self.assertEqual(policy.stdout.strip(), name.decode() + ": " + alternate.decode())

    def test_unrestricted_absolute_timezone_link_never_applies_attributes_to_host(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            root=Path(folder)/"system"
            parent=root/"System/Library/Templates/Data/private/var/db/timezone"
            parent.mkdir(parents=True)
            name=b"System/Library/Templates/Data/private/var/db/timezone/zoneinfo"
            target=b"/usr/share/zoneinfo.default"
            header=(b"TYP1LPATP"+struct.pack("<H",len(name))+name+
                    b"LNKP"+struct.pack("<H",len(target))+target+
                    b"UID1\0GID1\0MOD2\xff\x01FLG1\0")
            entry=b"YAA1"+struct.pack("<H",len(header)+6)+header
            archive=Path(folder)/"asset.zip"
            with zipfile.ZipFile(archive,"w") as writer:
                writer.writestr("AssetData/payloadv2/payload.000",
                                b"pbzx"+struct.pack(">QQQ",len(entry),len(entry),len(entry))+entry)
            parent.chmod(0o555)
            try:
                reply=subprocess.run((self.probe,archive,root,"--extract"),capture_output=True,text=True)
                self.assertEqual(reply.returncode,0,reply.stderr)
                self.assertEqual(os.readlink(parent/"zoneinfo"),target.decode())
                self.assertEqual(parent.stat().st_mode&0o777,0o555)
            finally:parent.chmod(0o755)

    def test_original_rootless_bytes_remain_in_a_detached_restorable_slot(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            root=Path(folder)/"system";(root/"Frameworks").mkdir(parents=True)
            name=b"Frameworks/Alias";target=b"/System/Library/Frameworks/iTunesLibrary.framework"
            attribute=b"com.apple.rootless\0Fixture"
            xattrs=struct.pack("<I",len(attribute)+4)+attribute
            header=(b"TYP1LPATP"+struct.pack("<H",len(name))+name+
                    b"LNKP"+struct.pack("<H",len(target))+target+
                    b"UID1\0GID1\0MOD2\xff\x01FLG4\0\0\x08\0"+
                    b"XATA"+struct.pack("<H",len(xattrs)))
            entry=b"YAA1"+struct.pack("<H",len(header)+6)+header+xattrs
            archive=Path(folder)/"asset.zip"
            with zipfile.ZipFile(archive,"w") as writer:
                writer.writestr("AssetData/payloadv2/payload.000",
                                b"pbzx"+struct.pack(">QQQ",len(entry),len(entry),len(entry))+entry)
            reply=subprocess.run((self.probe,archive,root,"--extract"),capture_output=True,text=True)
            self.assertEqual(reply.returncode,0,reply.stderr)
            self.assertEqual(subprocess.check_output(("/usr/bin/xattr","-sp","com.apple.rootlesr",root/"Frameworks/Alias")).strip(),b"Fixture")

    def test_restrictive_directory_modes_do_not_block_the_next_chunk(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            root=Path(folder)/"system";root.mkdir()
            def directory(path,mode):
                return row(path,b"UID1\0GID1\0MOD2"+struct.pack("<H",mode)+b"FLG1\0").replace(b"TYP1F",b"TYP1D",1)
            try:
                for index,entry in enumerate((directory("restricted",0o600),
                                               directory("restricted/child",0o755))):
                    archive=Path(folder)/f"chunk-{index}.zip"
                    with zipfile.ZipFile(archive,"w") as writer:
                        writer.writestr("AssetData/payloadv2/payload.000",
                            b"pbzx"+struct.pack(">QQQ",len(entry),len(entry),len(entry))+entry)
                    reply=subprocess.run((self.probe,archive,root,"--extract"),capture_output=True,text=True)
                    self.assertEqual(reply.returncode,0,reply.stderr)
                self.assertTrue((root/"restricted/child").is_dir())
            finally:
                if((root/"restricted").exists()): (root/"restricted").chmod(0o700)

    def test_data_protection_restores_regular_file_labels_removed_by_compression(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            data=Path(folder)/"DataVolume";data.mkdir();(data/"owner").write_bytes(b"fixture")
            attr=b"com.apple.rootless\0Fixture";xattrs=struct.pack("<I",len(attr)+4)+attr
            fields=b"UID1\0GID1\0MOD2\xa4\x01FLG1\0XATA"+struct.pack("<H",len(xattrs))
            entry=row("System/Library/Templates/Data/owner",fields)+xattrs
            timezone=data/"private/var/db/timezone"
            timezone.mkdir(parents=True);timezone.chmod(0o555)
            directory_fields=b"UID1\0GID1\0MOD2\x6d\x01FLG1\0XATA"+struct.pack("<H",len(xattrs))
            entry+=row("System/Library/Templates/Data/private/var/db/timezone",directory_fields).replace(
                b"TYP1F",b"TYP1D",1)+xattrs
            archive=Path(folder)/"asset.zip"
            with zipfile.ZipFile(archive,"w") as out:
                out.writestr("AssetData/payloadv2/fixup.manifest",b"pbzx"+struct.pack(">QQQ",len(entry),len(entry),len(entry))+entry)
                out.writestr("AssetData/payloadv2/firmlinks_payload",row(".",b"UID1\0GID1\0MOD2\xed\x01FLG1\0"))
            for mode in (0o644, 0o444):
                with self.subTest(mode=oct(mode)):
                    (data/"owner").chmod(mode)
                    result=subprocess.run((self.probe,data,archive,"--data-protection"),capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(subprocess.check_output(("/usr/bin/xattr","-p","com.apple.rootlesr",data/"owner")).strip(),b"Fixture")
                    self.assertEqual((data/"owner").stat().st_mode & 0o7777, mode)
                    self.assertEqual(subprocess.check_output(("/usr/bin/xattr","-p","com.apple.rootlesr",timezone)).strip(),b"Fixture")
                    self.assertEqual(timezone.stat().st_mode & 0o7777, 0o555)

    def test_data_protection_keeps_existing_native_labels_without_duplicate_slots(self):
        with tempfile.TemporaryDirectory(dir=self.root) as folder:
            data=Path(folder)/"DataVolume";data.mkdir()
            for value in (b"",b"Fixture"):
                with self.subTest(value=value):
                    target=data/"owner";target.write_bytes(b"fixture")
                    native="com.virtualintelmac.test.native-rootless"
                    subprocess.run(("/usr/bin/xattr","-wx",native,value.hex(),target),check=True)
                    attr=b"com.apple.rootless\0"+value;xattrs=struct.pack("<I",len(attr)+4)+attr
                    fields=b"UID1\0GID1\0MOD2\xa4\x01FLG1\0XATA"+struct.pack("<H",len(xattrs))
                    entry=row("System/Library/Templates/Data/owner",fields)+xattrs
                    archive=Path(folder)/"asset.zip"
                    with zipfile.ZipFile(archive,"w") as out:
                        out.writestr("AssetData/payloadv2/fixup.manifest",b"pbzx"+struct.pack(">QQQ",len(entry),len(entry),len(entry))+entry)
                        out.writestr("AssetData/payloadv2/firmlinks_payload",row(".",b"UID1\0GID1\0MOD2\xed\x01FLG1\0"))
                    for existing_slot in (False,True):
                        if existing_slot: subprocess.run(("/usr/bin/xattr","-wx","com.apple.rootlesr",value.hex(),target),check=True)
                        target.chmod(0o444)
                        try:
                            result=subprocess.run((self.probe,data,archive,"--data-protection-fixture"),capture_output=True,text=True)
                            self.assertEqual(result.returncode,0,result.stderr)
                            self.assertEqual(bytes.fromhex(subprocess.check_output(("/usr/bin/xattr","-px",native,target),text=True)),value)
                            self.assertNotIn("com.apple.rootlesr",subprocess.check_output(("/usr/bin/xattr",target),text=True).splitlines())
                            self.assertEqual(target.stat().st_mode & 0o7777,0o444)
                        finally: target.chmod(0o644)
                    # A genuinely different label must not be silently accepted
                    # or leave a second slot for the detached catalog to rename.
                    subprocess.run(("/usr/bin/xattr","-w",native,"different",target),check=True)
                    result=subprocess.run((self.probe,data,archive,"--data-protection-fixture"),capture_output=True,text=True)
                    self.assertNotEqual(result.returncode,0)
                    self.assertEqual(subprocess.check_output(("/usr/bin/xattr","-p",native,target)).strip(),b"different")
                    self.assertNotIn("com.apple.rootlesr",subprocess.check_output(("/usr/bin/xattr",target),text=True).splitlines())

    @unittest.skipUnless(os.getenv("VIM_ASSISTANT_FIXUP_FIXTURE"), "Original Apple asset not configured")
    def test_original_installer_manifest(self):
        reply = subprocess.run((self.probe, os.environ["VIM_ASSISTANT_FIXUP_FIXTURE"]),
                               capture_output=True, text=True)
        self.assertEqual(reply.returncode, 0, reply.stderr)
        self.assertGreater(int(reply.stdout.strip()), 100000)


if __name__ == "__main__":
    unittest.main()
