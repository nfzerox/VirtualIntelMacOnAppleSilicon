#!/bin/zsh
# Finder entry point; Terminal remains open to show errors and next steps.
repo=${0:A:h}
/bin/zsh "$repo/scripts/pvg-setup" "$@"
result=$?
if [[ -t 0 && $# == 0 ]]; then
    read '?Press Return to close this installer. '
fi
exit $result
