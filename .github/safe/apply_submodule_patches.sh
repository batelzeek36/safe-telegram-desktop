#!/usr/bin/env bash
# Safe Telegram: apply the fork's patches to upstream submodules, each once.
#   .github/safe/<submodule>/*.patch -> Telegram/<submodule> (e.g. lib_ui)
set -euo pipefail

root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
for dir in "$root"/.github/safe/lib_*/; do
	[ -d "$dir" ] || continue
	name="$(basename "$dir")"
	for patch in "$dir"*.patch; do
		[ -e "$patch" ] || continue
		if git -C "$root/Telegram/$name" apply --reverse --check "$patch" 2>/dev/null; then
			echo "already applied: $name/$(basename "$patch")"
		else
			git -C "$root/Telegram/$name" apply "$patch"
			echo "applied: $name/$(basename "$patch")"
		fi
	done
done
