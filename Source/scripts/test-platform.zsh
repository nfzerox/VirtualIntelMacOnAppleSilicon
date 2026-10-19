#!/bin/zsh
# Cheap regression coverage: version routing must not pin guest patch releases.
set -euo pipefail
repo=${0:A:h:h}
source "$repo/scripts/pvg-platform"
for family in 10.14 10.15; do
    for version in $family $family.0 $family.1 $family.5 $family.6 $family.7 $family.8 $family.99; do
        pvg_platform "$version"
        [[ $pvg_target == $family ]]
        if [[ $family == 10.14 ]]; then
            [[ $pvg_name == Mojave && $pvg_release == "$repo/prebuilt/mojave" ]]
        else
            [[ $pvg_name == Catalina && $pvg_release == "$repo/prebuilt" ]]
        fi
    done
done
for version in 10.13.6 10.140.1 10.150.1 10.16 11.0 ''; do
    if pvg_platform "$version" 2>/dev/null; then
        print -u2 "Incorrectly accepted unsupported version: $version"
        exit 1
    else
        [[ $? == 65 ]]
    fi
done
print 'Platform routing passed: all Mojave/Catalina patch releases accepted; other families rejected.'
