"""Host-only installer development harness for classic HFS+ recipes."""

import argparse
from contextlib import ExitStack, contextmanager
import getpass
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import secrets
import shutil
import stat
import subprocess
import tempfile
import time
from uuid import uuid4

from host_install_hfs_metadata import restore_metadata
from host_install_hfs_recovery import RECOVERY_SECTORS, add_recovery
from host_install_tiger_netinfo import provision_account, read_store, shadow_hash, write_store
from host_install_tiger_packages import DEFAULT_PACKAGES, DITTO, catalog_permissions, package_path, populate_packages
import host_install_leopard_packages as leopard_packages
import host_install_snow_packages as snow_packages


HDI = "/usr/bin/hdiutil"
SUPPORT = Path("Library/Application Support/Virtual Intel Mac")
STATE = SUPPORT / "State/LegacySetup"  # Native controls built for pre-10.6.
BUILDS = {"tiger": "8R4088", "leopard": "9J3050", "snowleopard": "10J3250",
          "lion": "11G63", "mountainlion": "12F45", "mavericks": "13F34", "yosemite": "14F27",
          "elcapitan": "15G31", "sierra": "16G29", "highsierra": "17G66", "mojave": "18G103",
          "catalina": "19H15"}
TITLES = {"tiger": "Tiger", "leopard": "Leopard", "snowleopard": "Snow Leopard",
          "lion": "Lion", "mountainlion": "Mountain Lion", "mavericks": "Mavericks", "yosemite": "Yosemite",
          "elcapitan": "El Capitan", "sierra": "Sierra", "highsierra": "High Sierra", "mojave": "Mojave",
          "catalina": "Catalina"}
LION_PACKAGES = (
    "BaseSystemBinaries", "BaseSystemResources", "Essentials", "BootCamp", "BSD",
    "PodcastCapture", "JavaTools", "RemoteDesktop", "SIUResources", "AdditionalEssentials",
    "AdditionalSpeechVoices", "AsianLanguagesSupport", "MediaFiles", "MigrationAssistant",
    "Mail", "AddressBook", "iCal", "Automator", "DVDPlayer", "iTunes", "iChat",
    "JavaEssentials", "Safari", "OxfordDictionaries", "Japanese", "German",
    "French", "Spanish", "Italian", "Dutch", "Danish", "Finnish", "Korean",
    "Norwegian", "Russian", "Swedish", "BrazilianPortuguese", "SimplifiedChinese",
    "TraditionalChinese", "Portuguese", "Polish", "Arabic", "Hungarian", "Turkish",
    "Czech", "Thai", "Catalan", "Croatian", "Greek", "Hebrew", "Romanian",
    "Slovak", "Ukrainian", "X11User",
)
STANDARD_ESD_PACKAGES = (
    "BaseSystemBinaries", "BaseSystemResources", "Essentials", "BSD", "JavaTools",
    "AdditionalEssentials", "AdditionalSpeechVoices", "AsianLanguagesSupport",
    "MediaFiles", "JavaEssentials", "OxfordDictionaries", "X11redirect",
)
ESD_PACKAGES = {
    "lion": LION_PACKAGES,
    "mountainlion": (
        "BaseSystemBinaries", "BaseSystemResources", "Essentials", "BSD", "JavaTools",
        "RemoteDesktop", "AdditionalEssentials", "AdditionalSpeechVoices",
        "AsianLanguagesSupport", "MediaFiles", "JavaEssentials", "OxfordDictionaries", "X11redirect",
    ),
    "mavericks": STANDARD_ESD_PACKAGES,
    "yosemite": STANDARD_ESD_PACKAGES,
    "elcapitan": ("BaseSystemResources", "Essentials", "X11redirect"),
    "sierra": ("BaseSystemResources", "Essentials", "FirmwareUpdate", "EmbeddedOSFirmware"),
}
STARTUP_CACHE = "System/Library/Caches/com.apple.kext.caches/Startup/kernelcache"
PRELINKED_CACHE = "System/Library/PrelinkedKernels/prelinkedkernel"
SYSTEM_KERNEL = "System/Library/Kernels/kernel"
# Yosemite and El Capitan install two files with the same prelinked bytes.
PRELINKED_LAYOUTS = {
    "mavericks": ("mach_kernel", (STARTUP_CACHE,)),
    "yosemite": (SYSTEM_KERNEL, (PRELINKED_CACHE, STARTUP_CACHE)),
    "elcapitan": (SYSTEM_KERNEL, (PRELINKED_CACHE, STARTUP_CACHE)),
}
RECOVERY_CACHE_PATHS = {"elcapitan": PRELINKED_CACHE, "sierra": PRELINKED_CACHE}
NESTED_ESD_GUESTS = PRELINKED_LAYOUTS.keys() | RECOVERY_CACHE_PATHS.keys()
DIRECTORY_SERVICE_GUESTS = ESD_PACKAGES.keys() | {"highsierra", "mojave", "catalina", "bigsur"}
SINGLE_SYSTEM_APFS_GUESTS = {"highsierra", "mojave"}
APFS_GUESTS = SINGLE_SYSTEM_APFS_GUESTS | {"catalina"}
APFS_CHECKPOINT = Path("State/HostAssemblyCheckpoint.plist")
LOGIN_KEY = bytes.fromhex("7d895223d2bcddeaa3b91f")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def setup_state(guest):
    return STATE if guest in ("tiger", "leopard") else Path("private/var/db/VirtualIntelMac/LegacySetup")


def plist(path):
    return plistlib.loads(path.read_bytes())


def check_output(*command):
    return subprocess.check_output(command)


def run(*command):
    subprocess.run(command, check=True, stdin=subprocess.DEVNULL)


def attach(*options):
    return plistlib.loads(check_output(HDI, "attach", "-plist", "-nobrowse", "-noautoopen", *options))


@contextmanager
def mount_points():
    """Do not recursively clean up a mountpoint, even when ejection fails."""
    parent = Path(tempfile.mkdtemp(prefix="vim-host-install-"))
    media, root = parent / "source", parent / "target"
    media.mkdir()
    root.mkdir()
    try:
        yield media, root
    finally:
        for path in (root, media, parent):
            try:
                path.rmdir()
            except OSError:
                pass  # Leave diagnostic evidence or a busy mount intact.


class Target:
    def __init__(self, root, metadata):
        self.root = root
        self.metadata = metadata

    def mkdir(self, relative, mode=0o755):
        relative = Path(relative)
        if relative == Path("."):
            return
        path = self.root / relative
        if not path.exists():
            self.mkdir(relative.parent)
            path.mkdir(mode=mode)
            self.metadata[relative] = (0, 0, stat.S_IFDIR | mode)
        elif not path.is_dir() or path.is_symlink():
            raise ValueError(f"Not a target directory: {relative}")

    def write(self, relative, data, mode=0o644, *, replace=False):
        relative = Path(relative)
        self.mkdir(relative.parent)
        path = self.root / relative
        if replace:
            if not path.is_file() or path.is_symlink():
                raise ValueError(f"Expected a native file at {relative}")
            path.unlink()
        fd = os.open(path, os.O_WRONLY | os.O_EXCL | os.O_CREAT, mode)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        self.metadata[relative] = (0, 0, stat.S_IFREG | mode)

    def write_plist(self, relative, value, mode=0o644, *, replace=False):
        self.write(relative, plistlib.dumps(value), mode, replace=replace)

    def copy_file(self, source, relative, mode=0o644, *, replace=False):
        self.write(relative, source.read_bytes(), mode, replace=replace)

    def remember_tree(self, relative, uid, gid, *, receipt=False, support=False, replacement=False):
        pending = [Path(relative)]
        while pending:
            entry = pending.pop()
            mode = (self.root / entry).lstat().st_mode
            if stat.S_ISDIR(mode):
                pending.extend(entry / name for name in os.listdir(self.root / entry))
            if receipt:
                if stat.S_ISDIR(mode):
                    mode = stat.S_IFDIR | 0o775
                elif stat.S_ISREG(mode):
                    mode |= 0o664
            if replacement and not stat.S_ISLNK(mode):
                # Match the live installer's NormalizeReplacement. An
                # executable from the donor archive can be root-only, but
                # WindowServer and its clients must be able to map it.
                mode = stat.S_IFMT(mode) | (0o755 if stat.S_ISDIR(mode) or mode & 0o111 else 0o644)
            if support:
                # The host app can live on exFAT, where every entry
                # appears as 0700. GUI agents must be user-traversable
                # and executable; the private journals are separate.
                if stat.S_ISDIR(mode):
                    mode = stat.S_IFDIR | 0o755
                elif stat.S_ISREG(mode):
                    data = entry.suffix in (".plist", ".txt") or "Licenses" in entry.parts
                    mode = stat.S_IFREG | (0o644 if data else 0o755)
            self.metadata[entry] = uid, gid, mode


def stage_tiger_account_record(target, name, full_name, guid, *, smb=False):
    seed = Path("private/var/db/netinfo/local.nidb")
    records = read_store(target.root / seed)
    uid = provision_account(records, name, full_name, guid, smb=smb)
    # Replace only the stock database directory just unpacked onto this new volume.
    seed_path = target.root / seed
    original = seed_path.with_name("Stock.nidb")
    seed_path.rename(original)
    write_store(seed_path, records)
    shutil.rmtree(original)
    for path in list(target.metadata):
        if seed in path.parents:
            del target.metadata[path]
    target.remember_tree(seed, 0, 0)
    return uid


def stage_local_account_record(target, name, full_name, guid, password=None, *, pbkdf2=False):
    node = Path("private/var/db/dslocal/nodes/Default")
    # El Capitan's BOM and installed reference both use mode 0600 on this
    # directory. The guest's root service can traverse it; the unprivileged
    # host staging worker cannot. Keep the intended mode in the catalog
    # metadata and temporarily grant only owner search on our staging mount.
    staged_node = target.root / node
    mode = staged_node.lstat().st_mode
    if stat.S_ISDIR(mode) and not mode & stat.S_IXUSR:
        target.metadata.setdefault(node, (0, 0, mode))
        staged_node.chmod(stat.S_IMODE(mode) | stat.S_IXUSR)
    users = node / "users"
    records = [plist(path) for path in (target.root / users).glob("*.plist")]
    if any(name.casefold() == existing.casefold() for record in records
           for existing in record.get("name", ())):
        raise ValueError("Refusing to replace a stock directory record")
    used = {int(uid) for record in records for uid in record.get("uid", ())}
    uid = next(candidate for candidate in range(501, 65534) if candidate not in used)
    record = {"name": [name], "realname": [full_name], "uid": [str(uid)],
        "gid": ["20"], "home": [f"/Users/{name}"], "shell": ["/bin/bash"],
        "passwd": ["********"], "generateduid": [guid],
        "authentication_authority": [";ShadowHash;"]}
    if password is not None:
        if pbkdf2:
            # Mountain Lion stores a structured salted PBKDF2-SHA512 record.
            algorithm, salt, iterations = "SALTED-SHA512-PBKDF2", secrets.token_bytes(32), 20000
            hashed = {"salt": salt, "iterations": iterations,
                      "entropy": hashlib.pbkdf2_hmac("sha512", password, salt, iterations, 128)}
        else:
            # Lion's local account contains a 4-byte salt and SHA-512 digest.
            algorithm, salt = "SALTED-SHA512", secrets.token_bytes(4)
            hashed = salt + hashlib.sha512(salt + password).digest()
        record["ShadowHashData"] = [plistlib.dumps({algorithm: hashed}, fmt=plistlib.FMT_BINARY)]
        record["authentication_authority"] = [f";ShadowHash;HASHLIST:<{algorithm}>"]
    target.write_plist(users / f"{name}.plist", record, 0o600)
    path = node / "groups/admin.plist"
    admin = plist(target.root / path)
    admin.setdefault("users", []).append(name)
    admin.setdefault("groupmembers", []).append(guid)
    target.write_plist(path, admin, 0o600, replace=True)
    return uid


def stage_account(target, name, full_name, password, owner, autologin, ssh, *, system_root=None):
    guest = owner["Guest"]
    tiger_smb = guest == "tiger" and owner.get("FileSharing", False)
    guid = str(uuid4()).upper()
    if guest == "tiger":
        uid = stage_tiger_account_record(target, name, full_name, guid, smb=tiger_smb)
    else:
        uid = stage_local_account_record(target, name, full_name, guid,
                                         password if guest in DIRECTORY_SERVICE_GUESTS else None,
                                         pbkdf2=guest in DIRECTORY_SERVICE_GUESTS and guest != "lion")
    if guest not in DIRECTORY_SERVICE_GUESTS:
        shadow = Path("private/var/db/shadow")
        target.mkdir(shadow, 0o700)
        target.mkdir(shadow / "hash", 0o700)
        target.write(shadow / "hash" / guid, shadow_hash(password, smb=tiger_smb), 0o600)
    home = Path("Users") / name
    system_root = system_root or target.root
    # Big Sur's System/Library/User Template is a relative symlink into
    # Library on the paired Data volume, which isn't mounted at System here.
    templates = (target.root / "Library/User Template" if guest == "bigsur"
                 else system_root / "System/Library/User Template")
    shared = templates / "Non_localized"
    # Newer media put Documents, Downloads and shared Library defaults
    # outside the localized template. Merge the localized defaults last.
    if shared.is_dir():
        run(*DITTO, shared, target.root / home)
    run(*DITTO, templates / "English.lproj", target.root / home)
    if guest in DIRECTORY_SERVICE_GUESTS:
        if guest == "lion":
            preference = {"GestureMovieSeen": "none"}
        elif guest == "mountainlion":
            preference = {"LastSeenCloudProductVersion": "10.8.5"}
        elif guest == "mavericks":
            preference = {"LastSeenCloudProductVersion": "10.9.5", "LastSeenSyncProductVersion": "10.9.5",
                          "DidSeeSyncSetup": True, "DidSeeSyncSetup2": True}
        else:
            # These releases' loginwindow invokes MiniLauncher even when the
            # system-wide setup marker exists. It compares the saved per-user
            # cloud and sync versions with the running system before deciding
            # whether to launch Setup Assistant -MiniBuddyYes.
            system = plist(system_root / "System/Library/CoreServices/SystemVersion.plist")
            version = system["ProductVersion"]
            preference = {"LastSeenCloudProductVersion": version, "LastSeenSyncProductVersion": version,
                          "DidSeeSyncSetup": True, "DidSeeSyncSetup2": True,
                          "LastSeenBuddyBuildVersion": system["ProductBuildVersion"]}
        target.write_plist(home / "Library/Preferences/com.apple.SetupAssistant.plist",
                           {"DidSeeCloudSetup": True, **preference})
    target.remember_tree(home, uid, 20)
    target.write("private/var/db/.AppleSetupDone", b"", 0o644 if guest == "tiger" else 0o600)
    # The default packages include Tiger's separate post-login registration
    # assistant. A finished native first login leaves this empty marker
    # owned by the first administrator; .AppleSetupDone alone is not enough.
    if guest in ("tiger", "leopard", "snowleopard", "lion"):
        registration = Path("Library/Receipts/.SetupRegComplete")
        target.write(registration, b"", 0o400)
        target.metadata[registration] = (uid if guest == "tiger" else 0, 80, stat.S_IFREG | 0o400)
    account = {**owner, "Username": name, "UID": uid, "GeneratedUID": guid,
               "FirstLoginPolicyPrepared": True,
               "FirstLoginPolicyMechanism": {
                   "tiger": "TigerNativeSetupDone", "leopard": "LeopardNativeSetupDone",
                   "snowleopard": "SnowLeopardNativeSetupDone", "lion": "LionNativeFirstLoginPreferences",
                   "mountainlion": "MountainLionNativeCloudPreferences",
                   "mavericks": "MavericksNativeCloudPreferences",
                   "yosemite": "YosemiteNativeMiniBuddyPreferences",
                   "elcapitan": "ElCapitanNativeMiniBuddyPreferences",
                   "sierra": "SierraNativeMiniBuddyPreferences",
                   "highsierra": "HighSierraNativeMiniBuddyPreferences",
                   "mojave": "MojaveNativeMiniBuddyPreferences",
                   "catalina": "CatalinaNativeMiniBuddyPreferences",
                   "bigsur": "BigSurNativeMiniBuddyPreferences",
               }[guest],
               "Phase": "autologin-armed" if autologin else "complete"}
    if tiger_smb:
        # The original disc contains the template; native setup makes the
        # active copy. Merely loading launchd's socket cannot replace it.
        target.copy_file(target.root / "private/etc/smb.conf.template", "private/etc/smb.conf", 0o644)
        account.update(FileSharingAccountPrepared=True, FileSharingCredentialVersion=1,
                       FileSharingDeferredUntilDesktop=True, HostStagedFileSharing=True)
    if autologin:
        plain = password + b"\0"
        padded = plain + secrets.token_bytes((len(password) // 12 + 1) * 12 - len(plain))
        encoded = bytes(byte ^ LOGIN_KEY[i % len(LOGIN_KEY)] for i, byte in enumerate(padded))
        target.write("private/etc/kcpassword", encoded, 0o600)
        pref = "Library/Preferences/com.apple.loginwindow.plist"
        previous = plist(target.root / pref) if (target.root / pref).exists() else {}
        target.write_plist(pref, {**previous, "autoLoginUser": name},
                           replace=(target.root / pref).exists())
        account["AutoLoginSHA256"] = digest(encoded)
    if ssh:
        if guest == "bigsur":
            # launchctl enable system/com.openssh.sshd persists this override
            # on Data, leaving the authenticated LaunchDaemon unchanged.
            path = Path("private/var/db/com.apple.xpc.launchd/disabled.plist")
            exists = (target.root / path).is_file()
            disabled = plist(target.root / path) if exists else {}
            target.mkdir(path.parent, 0o700)
            target.write_plist(path, {**disabled, "com.openssh.sshd": False}, 0o644, replace=exists)
        else:
            path = "System/Library/LaunchDaemons/ssh.plist"
            native = plist(target.root / path)
            native["Disabled"] = False
            target.write_plist(path, native, replace=True)
        account["RemoteLoginConfigured"] = True
    target.write_plist(setup_state(guest) / "Account.plist", account, 0o600)


def stage_efi_media_probe(target, build, state):
    # Use the same native, non-automounting FAT32 registration as the
    # installed-system maintenance helper, but before Disk Arbitration runs.
    path = Path("System/Library/Filesystems/msdos.fs/Contents/Info.plist")
    source = target.root / path
    info = plist(source)
    types = info["FSMediaTypes"]
    efi = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
    if efi in types:
        return
    fat = types["DOS_FAT_32"]
    properties = fat["FSMediaProperties"]
    if properties["Content Hint"] != "DOS_FAT_32" or not properties["Leaf"] or fat["FSProbeArguments"] != "-p":
        raise ValueError("The native FAT32 filesystem probe is not recognized")
    backup = state / "OriginalFilesystemMetadata"
    target.mkdir(backup, 0o700)
    target.mkdir(backup / build, 0o700)
    target.copy_file(source, backup / build / "msdos-Info.plist", 0o600)
    types[efi] = {**fat, "FSMediaProperties": {**properties, "Content Hint": efi},
                  "autodiskmount": False}
    target.write_plist(path, info, replace=True)


def stage_support(target, support, owner, build):
    guest = owner["Guest"]
    state = setup_state(guest)
    tools = SUPPORT / "GuestTools"
    launch = Path("Library/LaunchDaemons")
    agent = "com.mac.virtualintelmac.opengl-agent"
    ownership = {**owner, "Phase": "native-files-installed", "InstallOnly": False}
    if guest == "tiger":
        controls = support / "BuildReceipt.plist"
        manifest = plist(controls)
        target.copy_file(controls, tools / "BuildReceipt.plist")
        for name in manifest["Files"]:
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
                raise ValueError("Invalid control filename")
            target.copy_file(support / name, tools / name, 0o644 if name.endswith((".plist", ".txt")) else 0o755)
        target.write_plist(launch / f"{agent}.plist", {"Label": agent, "RunAtLoad": True,
            "OnDemand": False, "ProgramArguments": [f"/{tools}/opengl-guest-agent", "--device", "/dev/cu.serial2"]})
        ownership.update(GuestAgentSHA256=digest((support / "opengl-guest-agent").read_bytes()),
                         GuestToolsBuildReceiptSHA256=digest(controls.read_bytes()))
    else:
        target.mkdir(tools)
        run(*DITTO, support, target.root / tools)
        target.remember_tree(tools, 0, 0, support=True)
        target.copy_file(support / f"{agent}.plist", launch / f"{agent}.plist")
        display = "com.virtualintelmac.opengl-display.plist"
        target.copy_file(support / display, Path("Library/LaunchAgents") / display)
        if guest == "lion":
            # The installed Lion bootstrap otherwise changes this native
            # AnyUser/CurrentHost policy after WindowServer has already read
            # it and asks for a second boot. Prepare it on the new disk.
            path = Path("Library/Preferences/com.apple.windowserver.plist")
            present = (target.root / path).exists()
            preferences = plist(target.root / path) if present else {}
            if "DisplayResolutionEnabled" not in preferences and not preferences.get("DisplayResolutionDisabled"):
                preferences["DisplayResolutionEnabled"] = True
                target.write_plist(path, preferences, replace=present)
    target.mkdir(state, 0o700)
    target.write_plist(state / "Ownership.plist", ownership, 0o600)

    resources = Path("System/Library/Frameworks/OpenGL.framework/Versions/A/Resources")
    engine = resources / "GLEngine.bundle/GLEngine"
    native = (target.root / engine).read_bytes()
    native_hash = digest(native)
    graphics = plist(support / "Graphics.plist") if guest == "tiger" else None
    if graphics and graphics["NativeEngineSHA256"] != native_hash:
        raise ValueError("Tiger graphics payload does not match the installer engine")
    originals = state / "OriginalGraphics"
    target.mkdir(originals, 0o700)
    target.mkdir(originals / build, 0o700)
    target.write(originals / build / "GLEngine", native, 0o600)
    target.write(resources / "GLEngineStock.bundle/GLEngineStock", native, 0o755)
    target.copy_file(support / "GLEngine", engine, 0o755, replace=True)
    backend = "libVIMPassthroughOpenGL.dylib"
    target.copy_file(support / backend, engine.parent / backend, 0o755)
    ga = Path("VMsvga2GA.plugin/Contents")
    for name, mode in (("Info.plist", 0o644), ("MacOS/VMsvga2GA", 0o755)):
        target.copy_file(support / ga / name, Path("System/Library/Extensions") / ga / name, mode)
    evidence = {**owner, "FormatVersion": 1,
        "ProductBuildVersion": build, "OriginalSourceBuild": build, "OriginalSHA256": native_hash,
        "PreviousEngineSHA256": native_hash,
        "EngineSHA256": digest((support / "GLEngine").read_bytes()),
        "BackendSHA256": digest((support / backend).read_bytes()),
        "GASHA256": digest((support / ga / "MacOS/VMsvga2GA").read_bytes()),
        "GAInfoSHA256": digest((support / ga / "Info.plist").read_bytes()),
        "Phase": "installed", "ChangedThisBoot": False, "PrebootInstall": True,
        "DesktopQualified": False}
    if graphics:
        evidence["NativeDispatchCount"] = graphics["DispatchCount"]
    target.write_plist(state / "Graphics.plist", evidence, 0o600)

    if guest != "tiger":
        stage_efi_media_probe(target, build, state)
        return  # Leopard and Snow use the installed-system firmware's audio driver.
    audio = support / "Audio"
    audio_receipt = audio / "BuildReceipt.plist"
    files = {}
    for name, mode in (("VoodooHDA.kext/Contents/Info.plist", 0o644),
                       ("VoodooHDA.kext/Contents/MacOS/VoodooHDA", 0o755)):
        target.copy_file(audio / name, SUPPORT / "NativeAudio" / name, mode)
        files[name] = digest((audio / name).read_bytes())
    target.copy_file(audio_receipt, SUPPORT / "NativeAudio/BuildReceipt.plist")
    target.write_plist(state / "Audio.plist", {**owner, "FormatVersion": 1, "Files": files,
        "BuildReceiptSHA256": digest(audio_receipt.read_bytes()), "Phase": "installed"}, 0o600)
    sound = "com.mac.virtualintelmac.tiger-audio"
    target.write_plist(launch / f"{sound}.plist", {"Label": sound, "RunAtLoad": True, "OnDemand": True,
        "ProgramArguments": [f"/{tools}/opengl-tiger-audio", "--load"],
        "StandardErrorPath": "/var/log/VirtualIntelMacTigerAudio.log"})


def stage_esd_recovery(media, image, core_services, *, base_boot=None, base_cache=STARTUP_CACHE):
    """Reproduce the nonjournaled Recovery HD from authentic InstallESD files."""
    run(HDI, "create", "-sectors", str(RECOVERY_SECTORS), "-type", "SPARSEBUNDLE",
        "-layout", "NONE", "-fs", "HFS+", "-volname", "Recovery HD", image)
    with mount_points() as (_, root):
        attach("-owners", "off", "-mountpoint", root, image)
        try:
            target = Target(root, {})
            boot = Path("com.apple.recovery.boot")
            target.mkdir(boot)
            sources = {name: media / name for name in ("BaseSystem.dmg", "BaseSystem.chunklist", "boot.efi")}
            system = media
            cache_name = "kernelcache"
            if base_boot is not None:
                system = base_boot
                sources["boot.efi"] = system / core_services / "boot.efi"
                cache_name = Path(base_cache).name
                sources[cache_name] = system / base_cache
            else:
                sources[cache_name] = media / cache_name
            for name, source in sources.items():
                run(*DITTO, source, root / boot / name)
                target.metadata[boot / name] = (0, 0, stat.S_IFREG | 0o644)
            target.copy_file(system / core_services / "PlatformSupport.plist", boot / "PlatformSupport.plist")
            target.copy_file(system / core_services / "SystemVersion.plist", boot / "SystemVersion.plist", 0o444)
            target.write_plist(boot / "com.apple.Boot.plist", {
                "Kernel Flags": "rp=file:///com.apple.recovery.boot/BaseSystem.dmg",
                "Kernel Cache": f"\\com.apple.recovery.boot\\{cache_name}",
            })
            target.write(".metadata_never_index", b"")
            target.write(".fseventsd/no_log", b"")
            permissions = catalog_permissions(root, target.metadata)
            blessing = (root / boot).stat().st_ino, (root / boot / "boot.efi").stat().st_ino
        finally:
            run(HDI, "detach", root)
    restore_metadata(image, 0, RECOVERY_SECTORS * 512, permissions, boot=blessing)


def stage_prelinked_kernel(target, base, stage, guest, kernel, paths, *,
                           repositories=("System/Library/Extensions",)):
    """Prelink this disk's original kernel and local-root kexts using the media tool."""
    root = target.root
    candidate = stage / "kernelcache"
    log = stage / "Logs/Prelink.log"
    # The tool processes a known-build, read-only Apple input and our new
    # guest only. Never use -update-volume on the host: that also manages
    # boot/helper partitions. Guest kexts are not loaded into the host kernel.
    with log.open("xb") as diagnostics:
        result = subprocess.run((
            "/usr/bin/arch", "-x86_64", base / "usr/sbin/kextcache",
            "-prelinked-kernel", candidate, "-kernel", root / kernel,
            "-volume-root", root, "-arch", "x86_64", "-local-root",
            "-no-authentication", *(root / path for path in repositories),
        ), stdin=subprocess.DEVNULL, stdout=diagnostics, stderr=subprocess.STDOUT,
            env={"PATH": "/usr/bin:/bin:/usr/sbin:/sbin"})
    if result.returncode or not candidate.is_file() or not candidate.stat().st_size:
        raise ValueError(f"{TITLES[guest]} could not prelink its startup kernel; see {log}")
    for path in paths:
        target.copy_file(candidate, path, 0o644, replace=(root / path).exists())
    candidate.unlink()


def stage_sierra_startup_cache(target):
    # Sierra Essentials already ships a prelinked cache for its kernel.
    # Keep it intact: our kernel extensions are supplied by the EFI, not
    # installed into this cache. Apple's normal updater can prelink in the VM.
    cache = target.root / PRELINKED_CACHE
    with cache.open("rb") as stream:
        if stream.read(8) not in (b"complzss", b"complzvn"):
            raise ValueError("The original Sierra startup cache was not found in Essentials")
    # Use the native legacy link, but avoid the installer's transient
    # /Volumes/<name> prefix when the installed disk becomes the root.
    link = Path(STARTUP_CACHE)
    target.mkdir(link.parent)
    (target.root / link).symlink_to("/" + PRELINKED_CACHE)
    target.metadata[link] = (0, 0, stat.S_IFLNK | 0o755)


def discard_partial_disks(stage):
    """Retain diagnostics, but do not make retry consume another whole disk."""
    root = stage.resolve()
    images = plistlib.loads(check_output(HDI, "info", "-plist"))["images"]
    if any(Path(image["image-path"]).resolve().is_relative_to(root) for image in images):
        return False  # A failed detach is recoverable; keep its backing image.
    for name in ("Staging.sparsebundle", "AppleFiles.sparsebundle", "Recovery.sparsebundle",
                 "Graphics.sparsebundle", "System.qcow2"):
        disk = root / "Disks" / name
        if disk.is_dir() and not disk.is_symlink():
            shutil.rmtree(disk)
        elif disk.exists() or disk.is_symlink():
            disk.unlink()
    return True


def assemble(args, password):
    output = args.output.resolve()
    if output.suffix != ".vintelmac" or output.exists():
        raise ValueError("Choose a new .vintelmac output path")
    stage = Path(tempfile.mkdtemp(prefix=f".{args.guest}-host-", dir=output.parent))
    try:
        build_machine(args, password, output, stage)
    except (Exception, KeyboardInterrupt):
        # The original failure is more actionable than a secondary error
        # listing mounts or cleaning up a partial image.
        try:
            removed = not (stage / APFS_CHECKPOINT).is_file() and discard_partial_disks(stage)
        except (OSError, ValueError, KeyError, subprocess.SubprocessError):
            removed = False
        message = "Removed the unpublished disk images" if removed else "Kept the unpublished disk images"
        print(f"{message}. Diagnostics: {stage / 'Logs'}", flush=True)
        raise


def assembly_checkpoint(args):
    checkpoint = getattr(args, "checkpoint", None)
    if checkpoint:
        checkpoint()


def build_machine(args, password, output, stage):
    if args.guest == "catalina":
        from host_install_catalina import build_catalina
        return build_catalina(args, password, output, stage)
    if args.guest in SINGLE_SYSTEM_APFS_GUESTS:
        from host_install_single_system_apfs import build_single_system_apfs
        return build_single_system_apfs(args, password, output, stage)
    app = args.app / "Contents/Resources/Runtime"
    assembly_checkpoint(args)
    guest = args.guest
    title = TITLES[guest]
    identifier = str(uuid4()).upper()
    for name in ("Disks", "Firmware", "State", "Logs", "Installation"):
        (stage / name).mkdir(mode=0o700)
    image = stage / "Disks/Staging.sparsebundle"
    recovery = stage / "Disks/Recovery.sparsebundle" if guest in ESD_PACKAGES else None
    print(f"Creating a native {title} disk", flush=True)
    run(HDI, "create", "-size", f"{args.size}g", "-type", "SPARSEBUNDLE", "-layout",
        "GPTSPUD", "-fs", "Journaled HFS+", "-volname", "Macintosh HD", image)
    if recovery:
        # Native diskmanagementd leaves seven unallocated sectors before
        # the backup GPT. The EFI partition ends at LBA 409639.
        sectors = args.size * (2**30 // 512) - 40 - 409640 - RECOVERY_SECTORS
        run(HDI, "resize", "-partitiononly", "-partitionID", "2", "-sectors", str(sectors), image)
    with mount_points() as (media, root), ExitStack() as sources:
        attach("-readonly", "-noverify", "-owners", "off", "-mountpoint", media, args.media)
        try:
            base = None
            if guest in ESD_PACKAGES:
                base, _ = sources.enter_context(mount_points())
                attach("-readonly", "-noverify", "-owners", "off", "-mountpoint", base, media / "BaseSystem.dmg")
                sources.callback(run, HDI, "detach", base)
            attach("-owners", "off", "-mountpoint", root, image)
            try:
                core_services = Path("System/Library/CoreServices")
                system = core_services / "SystemVersion.plist"
                build_source = base if guest in NESTED_ESD_GUESTS else media
                build = plist(build_source / system)["ProductBuildVersion"]
                if build != BUILDS[guest]:
                    raise ValueError(f"The {title} recipe requires original {BUILDS[guest]} restore media")
                disk = plistlib.loads(check_output("/usr/sbin/diskutil", "info", "-plist", root))
                owner = {"FormatVersion": 1, "Kind": "VirtualIntelMacLegacySetup", "Guest": guest,
                         "MachineIdentifier": identifier, "VolumeUUID": disk["VolumeUUID"], "Enabled": True,
                         "AutomaticLogin": args.auto_login, "RemoteLogin": args.ssh,
                         "FileSharing": bool(guest == "tiger" and getattr(args, "file_sharing", False))}
                print(f"Extracting Apple’s {title} packages", flush=True)
                if guest == "tiger":
                    target = Target(root, populate_packages(media, root, checkpoint=lambda: assembly_checkpoint(args)))
                    packages = DEFAULT_PACKAGES
                    for name in packages:
                        assembly_checkpoint(args)
                        receipt = Path("Library/Receipts") / f"{name}.pkg"
                        target.mkdir(receipt)
                        contents = package_path(media, name)
                        for member in ("Archive.bom", "Info.plist", "version.plist", "Resources"):
                            run(*DITTO, contents / member, root / receipt / "Contents" / member)
                        target.remember_tree(receipt, 0, 80, receipt=True)
                else:
                    if guest in ESD_PACKAGES:
                        packages = ESD_PACKAGES[guest]
                        metadata = snow_packages.populate_packages(media, root, stage, int(time.time()),
                                                                   packages, base_system=base)
                        target = Target(root, metadata)
                    else:
                        recipe = snow_packages if guest == "snowleopard" else leopard_packages
                        packages = recipe.PACKAGES
                        target = Target(root, recipe.populate_packages(media, root, stage, int(time.time())))
                    boot_path = Path("System/Library/CoreServices/boot.efi")
                    target.copy_file(root / "usr/standalone/i386/boot.efi", boot_path,
                                     replace=(root / boot_path).exists())
                    # Some classic tools look for this historical BSD marker,
                    # in addition to the PackageKit database entry.
                    bsd = Path("Library/Receipts/BSD.pkg")
                    if not (root / bsd).exists():
                        target.write(bsd, b"")
                    target.metadata[bsd] = (0, 80, stat.S_IFREG | 0o644)
                    # The native OSInstall postflight hides these standard
                    # paths from Finder; never follow their symlink targets.
                    hidden = ("bin", "cores", "dev", "etc", "mach_kernel", "private",
                              "sbin", "tmp", "usr", "var", "Volumes", "opt", "Network")
                    run("/usr/bin/chflags", "-h", "hidden",
                        *(root / path for path in hidden if os.path.lexists(root / path)))
                assembly_checkpoint(args)
                print("Preparing the account and graphics", flush=True)
                stage_support(target, app / f"OpenGLGuests/{guest}", owner, build)
                stage_account(target, args.username, args.full_name or args.username, password,
                              owner, args.auto_login, args.ssh)
                if guest in PRELINKED_LAYOUTS:
                    print(f"Prelinking {title}’s startup kernel on the host", flush=True)
                    stage_prelinked_kernel(target, base, stage, guest, *PRELINKED_LAYOUTS[guest])
                elif guest == "sierra":
                    stage_sierra_startup_cache(target)
                permissions = catalog_permissions(root, target.metadata)
                core = root / "System/Library/CoreServices"
                boot = core.stat().st_ino, (core / "boot.efi").stat().st_ino
            finally:
                run(HDI, "detach", root)
            if recovery:
                print(f"Preparing {title}’s native Recovery HD", flush=True)
                stage_esd_recovery(media, recovery, core_services, base_boot=base if guest in NESTED_ESD_GUESTS else None,
                                   base_cache=RECOVERY_CACHE_PATHS.get(guest, STARTUP_CACHE))
        finally:
            # The nested image must detach before its outer InstallESD.
            try:
                sources.close()
            finally:
                run(HDI, "detach", media)
    print(f"Finalizing the bootable {title} disk", flush=True)
    assembly_checkpoint(args)
    restore_metadata(image, disk["PartitionMapPartitionOffset"], disk["Size"], permissions, boot=boot)
    if recovery:
        add_recovery(image, recovery)

    assembly_checkpoint(args)
    finish_machine(args, app, output, stage, image, identifier, build, disk["VolumeUUID"], packages,
                   recovery=recovery)


def convert_staged_disk(qemu_img, raw, target, size):
    run(qemu_img, "convert", "-f", "raw", "-O", "qcow2", raw, target)
    info = json.loads(check_output(qemu_img, "info", "--output=json", target))
    if info.get("format") != "qcow2" or info.get("backing-filename") or info["virtual-size"] != size:
        raise ValueError("The new guest disk is not standalone")
    # Check container allocation/refcounts without modifying the unpublished
    # image. A correct virtual size alone doesn't mean conversion succeeded.
    run(qemu_img, "check", "-q", "-f", "qcow2", target)


def finish_machine(args, app, output, stage, image, identifier, build, volume_uuid, packages, *,
                   filesystem="HFS+", recovery=None, detached=False, keep_stage_image=False, assembly_details=None):
    guest = args.guest
    qemu_img = app / "bin/qemu-img"
    target = stage / "Disks/System.qcow2"

    def convert(raw):
        convert_staged_disk(qemu_img, raw, target, args.size * 2**30)

    if detached:
        # No kernel probe or mount of an image edited by the APFS finalizer.
        from host_install_apfs import attached_images, checker_scratch, copy_container
        from host_install_hfs_metadata import SparseBundle
        if attached_images(image):
            raise ValueError("Detach the finished guest image before exporting it")
        size = args.size * 2**30
        if image.is_file() and not image.is_symlink():
            convert(image)
        else:
            with checker_scratch(image.parent, size) as scratch:
                raw = scratch / "Disk.raw"
                copy_container(SparseBundle(image), raw, 0, size)
                convert(raw)
    else:
        attached = attach("-readonly", "-nomount", image)
        device, = (item["dev-entry"] for item in attached["system-entities"]
                   if item.get("content-hint") == "GUID_partition_scheme")
        try:
            if not re.fullmatch(r"/dev/disk\d+", device):
                raise ValueError("Unexpected device for staged GPT")
            convert(device.replace("/dev/disk", "/dev/rdisk"))
        finally:
            run(HDI, "detach", device)
    shutil.copyfile(app / f"Firmware/{guest}/VARS.fd", stage / "Firmware/NVRAM.fd")
    settings = {"FormatVersion": 1, "Identifier": identifier, "Guest": guest,
        "InstallState": "installed",
        "Disk": "Disks/System.qcow2", "DiskGiB": args.size, "DisplayMode": "scaled",
        "Firmware": "Firmware/CODE.fd", "FirmwareSource": "application",
        "NVRAM": "Firmware/NVRAM.fd", "BootMedia": "Firmware/EFI.img", "BootMediaKind": "efi-disk",
        "CPUCount": 2 if guest in ("tiger", "leopard") else 4,
        "MemoryMiB": 2048 if guest in ("tiger", "leopard") else 4096,
        "SSHEnabled": args.ssh, "SSHPort": args.ssh_port,
        "FileSharingEnabled": bool(guest == "tiger" and getattr(args, "file_sharing", False)), "FileSharingPort": 1445}
    if guest not in ("mojave", "catalina", "bigsur"):
        settings.update(LegacyDisplayAdapter="vmware", PassthroughOpenGLEnabled=True)
    (stage / "Machine.plist").write_bytes(plistlib.dumps(settings))
    (stage / "State/InstallationTarget.plist").write_bytes(plistlib.dumps({
        "FormatVersion": 1, "VolumeUUID": volume_uuid, "Filesystem": filesystem}))
    (stage / "State/HostAssembly.plist").write_bytes(plistlib.dumps({
        **(assembly_details or {}),
        "FormatVersion": 1, "Guest": guest, "ProductBuildVersion": build,
        "VolumeUUID": volume_uuid, "MachineIdentifier": identifier,
        "Phase": "awaiting-first-boot", "Packages": list(packages)}))
    # A standalone qualification seed can be retained independently of the
    # runnable VM. Normal installation keeps only the finished disk.
    if not keep_stage_image:
        shutil.rmtree(image)
    if recovery:
        shutil.rmtree(recovery)
    assembly_checkpoint(args)
    stage.rename(output)
    print(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guest", choices=BUILDS, default="tiger")
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--media", type=Path, required=True)
    parser.add_argument("--base-system", type=Path, help="Original sibling BaseSystem.dmg for APFS guests")
    parser.add_argument("--authorize-apfs-mounts", action="store_true",
                        help="Use terminal administrator approval only to mount and bless generated APFS boot volumes")
    parser.add_argument("--resume-apfs-stage", type=Path, help="Resume a retained, unpublished APFS assembly")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--username")
    parser.add_argument("--full-name")
    parser.add_argument("--size", type=int, default=12)
    parser.add_argument("--auto-login", action="store_true")
    parser.add_argument("--ssh", action="store_true")
    parser.add_argument("--ssh-port", type=int, default=23224)
    parser.add_argument("--password-fd", type=int, help="Read one UTF-8 password line from an inherited descriptor")
    args = parser.parse_args()
    if not 8 <= args.size <= 2048 or not 1024 <= args.ssh_port <= 65535:
        parser.error("Choose a disk of 8–2048 GiB and an unprivileged TCP port")
    if args.guest in APFS_GUESTS and args.size < 20:
        parser.error("Choose a disk of at least 20 GiB for an APFS guest")
    if args.guest in APFS_GUESTS and os.geteuid() == 0:
        parser.error("Run the APFS worker as a regular user; use --authorize-apfs-mounts for the auxiliary volumes if needed")
    if args.resume_apfs_stage:
        if args.guest not in SINGLE_SYSTEM_APFS_GUESTS:
            parser.error("Only a single-system APFS assembly can be resumed here")
        from host_install_single_system_apfs import resume_apfs_assembly
        resume_apfs_assembly(args)
        return
    if args.guest == "catalina" and not args.authorize_apfs_mounts:
        parser.error("Catalina's scratch System and Data volumes require --authorize-apfs-mounts")
    if not re.fullmatch(r"[a-z][a-z0-9_-]{0,30}", args.username or ""):
        parser.error("Choose a valid short account name")
    if args.password_fd is None:
        password = getpass.getpass().encode("utf-8")
    else:
        with os.fdopen(args.password_fd, "rb", closefd=False) as stream:
            password = stream.readline(513).removesuffix(b"\n")
    password.decode("utf-8")
    shadow_hash(password)  # Reject invalid credentials before creating a disk.
    assemble(args, password)


if __name__ == "__main__":
    main()
