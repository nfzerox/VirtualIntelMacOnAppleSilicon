// Installer/recovery admission only, shared by the source-built loader and
// its finite native probe. Apple system kexts still load normally. An actual
// installer needs no injected graphics, audio, input or system families.
// Keep the configured SMC/decryption provider and its Lilu dependency:
// QEMU's OSK device alone does not establish the DSMOS page-transform hook.
// No guest-version offsets, persistent flags, graphics hot-path work or timers.
STATIC BOOLEAN
VimUseNativeInstallerDrivers (
  IN BOOLEAN  HasFramebuffer,
  IN BOOLEAN  HasAccelerator,
  IN BOOLEAN  RecoveryBoot,
  IN BOOLEAN  HasInstallerDirectory
  )
{
  return HasFramebuffer && HasAccelerator && (RecoveryBoot || HasInstallerDirectory);
}

STATIC BOOLEAN
VimSkipInstallerKext (
  IN BOOLEAN      InstallerKernel,
  IN CONST CHAR8  *BundlePath
  )
{
  return InstallerKernel
    && AsciiStrCmp (BundlePath, "Lilu.kext") != 0
    && AsciiStrCmp (BundlePath, "VirtualSMC.kext") != 0;
}

// Sierra's recovery cache contains Drizzle's higher-score dummy SMC claimant,
// not the native AppleSMC/DSMOS pair. Let the native drivers match normally.
// Disable only its initializer; excluding its shared XML breaks ID references.
STATIC BOOLEAN
VimDisableInstallerDrizzle (
  IN BOOLEAN  InstallerKernel,
  IN UINT32   DarwinMajor
  )
{
  return InstallerKernel && DarwinMajor == 16;
}
