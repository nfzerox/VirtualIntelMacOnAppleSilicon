#!/bin/bash
# Development-only native SkyLight selection test. Run as root in Mojave.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Run as root'; exit 1; }
[[ $(sw_vers -productVersion) == 10.14.* ]] || exit 1
config=/System/Library/PrivateFrameworks/SkyLight.framework/Resources/Configuration.plist
backup=${PVG_BACKUP_ROOT:-$HOME}/pvg-backup-metal-selection
mkdir -p "$backup"
if [[ ! -f "$backup/Configuration.plist" ]]; then
    cp -p "$config" "$backup/Configuration.plist"
fi
# This key is read by Mojave activate_compositor_default's dispatch_once
# block AFTER checking WSSystemCanCompositeWithMetal. Not a capability spoof.
if /usr/libexec/PlistBuddy -c 'Print :BootToMetalCompositor' "$config" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c 'Set :BootToMetalCompositor true' "$config"
else
    /usr/libexec/PlistBuddy -c 'Add :BootToMetalCompositor bool true' "$config"
fi
plutil -lint "$config"
sync
echo "Native Metal selection enabled; original saved in $backup"
