Project-owned PVGIOSurfaceABI fast-build inputs
============================================

These small x86_64 guest kexts supersede the older copy inside the historical
donor archives. package-app-guest-payloads.py verifies SHA256SUMS and the code
signature, omits the old bundle completely, then packages this bundle.

Source: compat/PVGIOSurfaceABI, vendor/source/Lilu, vendor/source/MacKernelSDK.
No dependency checkout, Apple donor extraction or legacy Xcode is needed.
Run scripts/build-iosurface-abi with PVG_TARGET_OS=10.14 and 10.15. The selected
Xcode is used; PVG_DEVELOPER_DIR can explicitly select a different installation.
Both products are Intel guest drivers, even when built on an ARM host.

Build.command --rebuild-guest-tools (also --rebuild-runtime) rebuilds both guest
drivers into that build's GuestTools/IOSurfaceABI directory and packages those
new products instead of these fast-build inputs. This does not promote or
overwrite the checked-in binaries. Default fast builds reuse these binaries.

1.6.2 adjusts only AppleParavirtAccelerator's final default dimensions at startup.
The donor's own populator overwrites the generic initializer, so the hook must
run after that populator. It recovers the private field by cross-checking the
initializer, validator and populator's independent config pointer/store,
fails closed on unfamiliar layouts, preserves row-byte/alignment constraints,
and does not patch IOSurface or SkyLight userland binaries. The display surface
limit supports 5K and 6K configurations; runtime qualification is separate from
the build-time layout and checksum checks.

1.6.3 additionally rejects display submissions with no plane-0 IOSurface using
the donor's own accessor and kIOReturnBadArgument, matching its validator. This
prevents the donor's AppleParavirtDisplayPipe.cpp:428 assertion during older
WindowServer teardown. Valid submissions, resource ownership, and fence
completion are unchanged; rejection logging is limited to four messages.
