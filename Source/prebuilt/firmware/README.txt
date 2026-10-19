Unbranded x86_64 OVMF, used by all production and development app builds.
Rebuild: scripts/build-guest-firmware.py (pinned source and tool checksums).
EDK II plus required dependency sources are checked into vendor/edk2-source.tar.gz;
normal firmware rebuilding no longer clones or initializes any Git dependency.
Use --source-only --work NEW_DIRECTORY for quick offline source qualification.
BuildReceipt.plist pins the exact code bytes, architecture and source revision.
The existing firmware archive retains the compatible 4 MB NVRAM template.

Production configuration is RELEASE (MDEPKG_NDEBUG); DEBUG is an explicit
maintainer option, not the default. Both configurations use -Oz/LTO; RELEASE
removes assert/debug-only code, so it is not a general guest CPU performance fix.
RELEASE cold boot qualified on Intel/HVF with existing Catalina NVRAM, reaching
the accelerated desktop. The NVRAM template is byte-identical to the previous
DEBUG build, so existing machines keep their settings. Apple silicon/TCG boot
of this release build remains unqualified on this Intel host.
The no-logo/no-status-text source changes are
not an assertion that S3 resume is fixed; that separate issue remains open.
Upstream EDK II is BSD-2-Clause-Patent: https://github.com/tianocore/edk2
