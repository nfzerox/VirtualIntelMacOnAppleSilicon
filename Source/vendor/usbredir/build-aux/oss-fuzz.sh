#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later

set -eux -o pipefail

export LC_CTYPE=C.UTF-8

if [[ -z "${WORK:-}" ]]; then
    builddir="${PWD}/build"
else
    builddir="${WORK}/build"
fi

if [[ -d "$builddir" ]]; then
    # Meson only looks at the value of environment variables when options
    # are defined for the first time, i.e. not on reconfiguration (see also
    # https://bugs.freedesktop.org/show_bug.cgi?id=107313#c2). Consequently
    # object files aren't rebuilt when such options change, e.g. "c_args" from
    # "$CFLAGS". Removing all files in the build directory obviously solves that
    # problem.
    #
    # usbredir is sufficiently small to make a full rebuild acceptable. If that
    # were to change there'd be the following options:
    #
    # a) Use a build directory per build option set, e.g. by naming the
    # directory after a hash of the various relevant environment variables.
    #
    # b) Explicitly pass environment variables as their target option, e.g.
    # "-Dc_args=$CFLAGS". Relevant documentation:
    # https://mesonbuild.com/Reference-tables.html#language-arguments-parameter-names
    # https://mesonbuild.com/Reference-tables.html#compiler-and-linker-flag-environment-variables
    #
    find "$builddir" -mindepth 1 -print -delete
fi

config=(
    --default-library=static
    -Dprefix="${OUT:?}"

    -Dfuzzing=enabled
    -Dfuzzing-engine="${LIB_FUZZING_ENGINE:?}"
    -Dfuzzing-install-dir="${OUT:?}"

    # Fails to build on Ubuntu 16.04
    -Dtools=disabled

    # Don't use "-Wl,--no-undefined"
    -Db_lundef=false

    # Enable internal tests
    -Dextra-checks=true
    )

if ! meson setup "${config[@]}" -- "$builddir"; then
    cat "${builddir}/meson-logs/meson-log.txt" >&2 || :
    exit 1
fi

meson compile -C "$builddir" -v

meson install -C "$builddir"

exit 0
