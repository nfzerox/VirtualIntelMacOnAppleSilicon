#!/bin/bash
# Research-only guarded guest deployment. Run as root INSIDE Mavericks.
# Replaces only our already-installed pair; preserves original stock engine.
set -euo pipefail
if [[ $# != 6 || $EUID != 0 ]]; then
    echo 'Usage (guest root): install-mavericks-gli-checkpoint.sh STAGE BACKUP OLD_GLI_SHA OLD_CLIENT_SHA NEW_GLI_SHA NEW_CLIENT_SHA' >&2
    exit 2
fi
stage=$1 backup=$2
case "$(/usr/bin/sw_vers -productVersion)" in 10.9.*) ;; *) echo 'Mavericks only.' >&2;exit 2;; esac
case "$stage" in /var/tmp/vim-gli-stage-*) ;; *) echo 'Unexpected staging directory.' >&2;exit 2;; esac
case "$backup" in /var/tmp/vim-gli-before-*) ;; *) echo 'Unexpected backup directory.' >&2;exit 2;; esac
[[ ${stage#/var/tmp/} != */* && ${backup#/var/tmp/} != */* ]] || { echo 'Use direct children of /var/tmp.' >&2;exit 2; }
bundle=/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle
gli=$bundle/GLEngine
client=$bundle/libVIMPassthroughOpenGL.dylib
sha() { /usr/bin/shasum -a 256 "$1" | /usr/bin/awk '{print $1}'; }
[[ "$(sha "$gli")" == "$3" && "$(sha "$client")" == "$4" ]] || { echo 'Installed pair changed; no write performed.' >&2;exit 1; }
[[ "$(sha "$stage/GLEngine")" == "$5" && "$(sha "$stage/libVIMPassthroughOpenGL.dylib")" == "$6" ]] || { echo 'Staged pair mismatch; no write performed.' >&2;exit 1; }
for binary in GLEngine libVIMPassthroughOpenGL.dylib; do
    /usr/bin/lipo "$stage/$binary" -verify_arch i386 x86_64
    /usr/bin/codesign --verify "$stage/$binary"
done
[[ -f /System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngineStock.bundle/GLEngineStock ]] || {
    echo 'Original software engine missing; no write performed.' >&2;exit 1;
}
/bin/mkdir -m 700 "$backup" # Refuses to replace an earlier checkpoint.
/bin/cp -p "$gli" "$backup/GLEngine"
/bin/cp -p "$client" "$backup/libVIMPassthroughOpenGL.dylib"
complete=0 started=0
restore() {
    status=$?
    if [[ $complete == 0 && $started == 1 ]]; then
        # Restore the older GLI before its client; never overwrite a mapped
        # executable inode in place. All rollback originals stay retained.
        /bin/cp -p "$backup/GLEngine" "$gli.rollback"
        /bin/mv -f "$gli.rollback" "$gli"
        /bin/cp -p "$backup/libVIMPassthroughOpenGL.dylib" "$client.rollback"
        /bin/mv -f "$client.rollback" "$client"
        echo "Restored prior pair; backup retained at $backup" >&2
    fi
    exit "$status"
}
trap restore EXIT
/bin/cp "$stage/GLEngine" "$gli.next"
/bin/cp "$stage/libVIMPassthroughOpenGL.dylib" "$client.next"
/usr/sbin/chown root:wheel "$gli.next" "$client.next"
/bin/chmod 755 "$gli.next" "$client.next"
started=1
/bin/mv -f "$client.next" "$client"
/bin/mv -f "$gli.next" "$gli"
[[ "$(sha "$gli")" == "$5" && "$(sha "$client")" == "$6" ]]
complete=1
echo "Installed verified i386/x64 pair; backup retained at $backup"
echo 'Existing mapped processes still use their older inodes. Qualify a clean boot.'
