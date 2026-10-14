#!/bin/sh
# Put the tracked hooks where git will actually run them.
#
# .githooks/ is the version-controlled source. The live home is .git/hooks/, which
# is shared by every branch - a hook kept in the working tree is simply absent on
# any branch that does not carry it, so a fence written to protect main vanishes
# the moment you check out main.
#
# Run this after a fresh clone, and after pulling a change to any hook:
#
#     sh .githooks/install.sh
#
# It deliberately does NOT set core.hooksPath. Setting that REPLACES .git/hooks/
# entirely, which silently disables every other hook installed there - including
# any that predate this directory.
set -eu

root=$(git rev-parse --show-toplevel)
src="$root/.githooks"
dst="$root/.git/hooks"

for hook in pre-commit reference-transaction; do
    if [ ! -f "$src/$hook" ]; then
        echo "missing source: $src/$hook" >&2
        exit 1
    fi
    cp -f "$src/$hook" "$dst/$hook"
    chmod +x "$dst/$hook"
    echo "installed $dst/$hook"
done

if [ "$(git config --get core.hooksPath || true)" != "" ]; then
    echo
    echo "warning: core.hooksPath is set to '$(git config --get core.hooksPath)'."
    echo "that overrides .git/hooks/ and none of the above will run. unset it:"
    echo "    git config --unset core.hooksPath"
fi
