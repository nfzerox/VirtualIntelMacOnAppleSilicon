"""Prepare the app's PVG payload against an unpublished Mojave or Catalina system."""

from contextlib import contextmanager, ExitStack
from pathlib import Path
import shutil
import subprocess

from host_install_hfs import HDI, DITTO, attach, mount_points, plist, run


TARGETS = Path(__file__).resolve().parents[1] / "support/install-targets-mojave.txt"
CATALINA_TARGETS = TARGETS.with_name("install-targets.txt")
STOCK = Path("private/var/db/VirtualIntelMac/HostAssembly/OriginalGraphics")
CATALINA_ALIAS = Path("System/Library/Extensions/AppleMetalGLRenderer.bundle")


def publish_graphics(target, payload, build, *, guest="mojave"):
    # No live guest files are visible here. Until this disk is detached and
    # published, an interruption only invalidates this private build attempt.
    # Keep the exact originals (including stock bundle modes) for recovery.
    root = target.root.resolve()
    for name in (CATALINA_TARGETS if guest == "catalina" else TARGETS).read_text().splitlines():
        relative = Path(name.removeprefix("/"))
        source = payload / relative
        destination = root / relative
        alias = (guest == "catalina" and relative == CATALINA_ALIAS and source.is_symlink() and
                 source.readlink() == Path("AppleMetalOpenGLRenderer.bundle") and source.resolve().is_dir())
        if not alias and (source.is_symlink() or not (source.is_file() or source.is_dir())):
            raise ValueError(f"Prepared {guest} payload is missing {name}")
        if not destination.parent.resolve().is_relative_to(root):
            raise ValueError(f"Graphics target escapes its staged system: {name}")
        if destination.exists() or destination.is_symlink():
            backup = STOCK / build / relative
            target.mkdir(backup.parent, 0o700)
            destination.rename(root / backup)
            for key in list(target.metadata):
                if key.is_relative_to(relative):
                    target.metadata[backup / key.relative_to(relative)] = target.metadata.pop(key)
        target.mkdir(relative.parent)
        if alias:
            destination.symlink_to(source.readlink())
        else:
            run(*DITTO, source, destination)
        target.remember_tree(relative, 0, 0, replacement=True)

    policy = Path("Library/Preferences/com.apple.security.libraryvalidation.plist")
    present = (root / policy).exists()
    preferences = plist(root / policy) if present else {}
    target.write_plist(policy, {**preferences, "DisableLibraryValidation": True}, replace=present)


@contextmanager
def prepared_pvg_payload(system, runtime, stage, build, guest, *, host_tools=None):
    tools = host_tools or runtime / "GuestTools"
    prepare = tools / "guest-prepare"
    image = stage / "Disks/Graphics.sparsebundle"
    # The scratch drive may be ExFAT. Use a separate Mac filesystem for the
    # signed bundles and symlinks. guest-prepare also keeps its work outside
    # the target system, and never needs to run Apple's installer scripts.
    run(HDI, "create", "-size", "2g", "-type", "SPARSEBUNDLE", "-layout", "NONE", "-fs",
        "Journaled HFS+", "-volname", "Graphics Staging", image)
    log = stage / "Logs/Graphics.log"
    with mount_points() as (media, work), ExitStack() as mounts:
        for path, mount, options in ((runtime / f"GuestPayloads/{guest}.iso", media, ("-readonly",)),
                                     (image, work, ())):
            attach("-noverify", "-owners", "off", "-mountpoint", mount, *options, path)
            mounts.callback(run, HDI, "detach", mount)
        archive = next((media / name for name in ("payload.tar.xz", "payload.tar.gz")
                        if (media / name).is_file()), None)
        if archive is None:
            raise ValueError(f"The packaged {guest} support disc has no payload")
        directory = work / "Extracted"
        payload = directory / "payload"
        with log.open("xb") as diagnostics:
            for arguments in (("--extract", archive, directory), (system, payload, tools)):
                result = subprocess.run((prepare, *arguments), stdin=subprocess.DEVNULL,
                                        stdout=diagnostics, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise ValueError(f"The {guest} graphics payload could not be prepared; see {log}")
        report = plist(payload / ".VirtualIntelMacPreparation.plist")
        if report.get("Guest") != guest or report.get("ProductBuildVersion") != build or not report.get("Prepared"):
            raise ValueError(f"The prepared payload does not belong to this {guest} system")
        yield payload
    shutil.rmtree(image)


def stage_pvg_graphics(target, runtime, stage, build, guest):
    with prepared_pvg_payload(target.root, runtime, stage, build, guest) as payload:
        publish_graphics(target, payload, build, guest=guest)
