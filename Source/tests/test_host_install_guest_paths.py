from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "Uses native offline guest path handling")
class OfflineGuestPathsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.work.cleanup)
        root = Path(cls.work.name)
        tools = Path(__file__).resolve().parents[1] / "tools/HostInstall"
        source = root / "probe.m"
        source.write_text(r'''
#import "VIMGuestPath.h"
int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=4)return 64;
    NSError *error=nil;
    NSURL *url=VIMResolveGuestPath([NSURL fileURLWithPath:@(argv[1])],@(argv[2]),atoi(argv[3]),&error);
    if(!url){fprintf(stderr,"%ld\n",(long)error.code);return 1;}
    puts(url.fileSystemRepresentation);return 0;
}}
''')
        cls.probe = root / "probe"
        subprocess.run(("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                        "-framework", "Foundation", "-I", tools, source,
                        tools / "VIMGuestPath.m", "-o", cls.probe), check=True)

    def resolve(self, root, path, follow=True):
        return subprocess.run((self.probe, root, path, str(int(follow))), capture_output=True, text=True)

    def test_absolute_and_relative_guest_aliases_never_reach_host(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "Guest"
            (root / "private/etc").mkdir(parents=True)
            (root / "private/etc/policy").write_text("guest")
            (root / "etc").symlink_to("/private/etc")
            (root / "relative").symlink_to("private/etc")
            for path in ("etc/policy", "/etc/policy", "relative/policy"):
                result = self.resolve(root, path)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(Path(result.stdout.strip()), root / "private/etc/policy")

    def test_framework_current_alias_and_parent_components(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "Framework/Versions/A/Resources").mkdir(parents=True)
            (root / "Framework/Versions/Current").symlink_to("A")
            (root / "Framework/Resources").symlink_to("Versions/Current/Resources")
            (root / "Framework/Resources/obsolete").touch()
            result = self.resolve(root, "Framework/Resources/../Resources/obsolete")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout.strip()), root / "Framework/Versions/A/Resources/obsolete")

    def test_cleanup_does_not_follow_final_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "Guest"
            root.mkdir()
            (root / "old").symlink_to("/etc")
            result = self.resolve(root, "old", follow=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout.strip()), root / "old")

    def test_escape_root_and_symlink_cycles_are_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "escape").symlink_to("../outside")
            (root / "loop").symlink_to("loop")
            for path in ("/", "..", "escape/file", "loop/file", "missing/file"):
                result = self.resolve(root, path)
                self.assertEqual(result.returncode, 1, path)


if __name__ == "__main__":
    unittest.main()
