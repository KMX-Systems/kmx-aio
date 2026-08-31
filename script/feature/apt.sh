#!/usr/bin/env bash
# Shared apt helpers for the per-feature dependency installers.
#
# These installers are not only run by hand. source.qbs bootstraps optional dependencies from a Probe,
# so they also run in the middle of "qbs resolve" - where there is no terminal to type a sudo password
# into. An unconditional "apt-get update" therefore fails the resolve of a tree that already has every
# package, and it fails with "sudo: a password is required", which names neither the feature nor the
# dependency at fault. So what is already installed is established first, and apt is reached for only
# when something is genuinely missing.
#
# The installers that build a dependency into output/ (spdk, quic, opc_ua, someip) short-circuit on the
# built artifact instead and do not need this; the ones that only install packages have nothing to look
# for, which is what these helpers give them.

# Prints those of its arguments that dpkg does not report as installed, one per line.
apt_missing_packages() {
	local package
	for package in "$@"; do
		if ! dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q '^install ok installed$'; then
			printf '%s\n' "$package"
		fi
	done
}

# Installs whichever of the named packages are missing, and touches apt not at all when none are.
# $1 is the tag the calling installer logs under; the rest are package names.
apt_install_missing() {
	local tag="$1"
	shift

	local -a missing=()
	mapfile -t missing < <(apt_missing_packages "$@")

	if [[ ${#missing[@]} -eq 0 ]]; then
		echo "[${tag}] All required packages already installed: $*"
		return 0
	fi

	# Reached on a distribution without dpkg as well, where every package reads as missing. Naming them
	# is more use than the apt-get requirement on its own: they are what has to be installed by hand.
	if ! command -v apt-get >/dev/null 2>&1; then
		{
			echo "[${tag}] Missing packages: ${missing[*]}"
			echo "[${tag}] This script installs them with apt-get, which supports Ubuntu/Debian only."
			echo "[${tag}] Install this distribution's equivalents and run the build again."
		} >&2
		exit 1
	fi

	local sudo_prefix=""
	if [[ "${EUID}" -ne 0 ]]; then
		sudo_prefix="sudo"
	fi

	echo "[${tag}] Installing missing packages: ${missing[*]}"
	${sudo_prefix} apt-get update
	${sudo_prefix} apt-get install -y "${missing[@]}"
}
