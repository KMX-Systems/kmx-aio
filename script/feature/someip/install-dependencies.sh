#!/usr/bin/env bash
set -euo pipefail

detect_package_hint() {
	local package_name="$1"

	if [[ -f /etc/os-release ]]; then
		# shellcheck disable=SC1091
		source /etc/os-release
		local id="${ID:-}"
		local id_like="${ID_LIKE:-}"
		case "${ID:-}" in
			ubuntu|debian)
				echo "sudo apt-get install -y ${package_name}"
				return
				;;
			fedora)
				echo "sudo dnf install -y ${package_name}"
				return
				;;
			rhel|centos|rocky|almalinux)
				echo "sudo yum install -y ${package_name}"
				return
				;;
			arch)
				echo "sudo pacman -S --noconfirm ${package_name}"
				return
				;;
			opensuse*|sles)
				echo "sudo zypper install -y ${package_name}"
				return
				;;
		esac

		case " $id_like " in
			*" debian "*)
				echo "sudo apt-get install -y ${package_name}"
				return
				;;
			*" fedora "*)
				echo "sudo dnf install -y ${package_name}"
				return
				;;
			*" rhel "*)
				echo "sudo yum install -y ${package_name}"
				return
				;;
			*" arch "*)
				echo "sudo pacman -S --noconfirm ${package_name}"
				return
				;;
			*" suse "*)
				echo "sudo zypper install -y ${package_name}"
				return
				;;
		esac
	fi

	echo "install package: ${package_name}"
}

detect_distro_family() {
	if [[ -f /etc/os-release ]]; then
		# shellcheck disable=SC1091
		source /etc/os-release
		local id="${ID:-}"
		local id_like="${ID_LIKE:-}"

		case "$id" in
			ubuntu|debian)
				echo "debian"
				return
				;;
			fedora)
				echo "fedora"
				return
				;;
			rhel|centos|rocky|almalinux)
				echo "rhel"
				return
				;;
			arch)
				echo "arch"
				return
				;;
			opensuse*|sles)
				echo "suse"
				return
				;;
		esac

		case " $id_like " in
			*" debian "*)
				echo "debian"
				return
				;;
			*" fedora "*)
				echo "fedora"
				return
				;;
			*" rhel "*)
				echo "rhel"
				return
				;;
			*" arch "*)
				echo "arch"
				return
				;;
			*" suse "*)
				echo "suse"
				return
				;;
		esac
	fi

	echo "unknown"
}

package_for_dependency() {
	local family="$1"
	local dependency="$2"

	case "$dependency" in
		git)
			echo "git"
			;;
		cmake)
			echo "cmake"
			;;
		g++)
			case "$family" in
				debian)
					echo "g++"
					;;
				arch)
					echo "gcc"
					;;
				*)
					echo "gcc-c++"
					;;
			esac
			;;
		boost_dev)
			case "$family" in
				debian)
					echo "libboost-all-dev"
					;;
				arch)
					echo "boost"
					;;
				*)
					echo "boost-devel"
					;;
			esac
			;;
		*)
			echo ""
			;;
	esac
}

run_as_root() {
	if (( EUID == 0 )); then
		"$@"
		return
	fi

	if command -v sudo >/dev/null 2>&1; then
		sudo "$@"
		return
	fi

	echo "Need root privileges to install packages, but sudo was not found." >&2
	return 1
}

install_packages() {
	local family="$1"
	shift
	local -a packages=("$@")

	case "$family" in
		debian)
			run_as_root apt-get update
			run_as_root apt-get install -y "${packages[@]}"
			;;
		fedora)
			run_as_root dnf install -y "${packages[@]}"
			;;
		rhel)
			run_as_root yum install -y "${packages[@]}"
			;;
		arch)
			run_as_root pacman -S --noconfirm "${packages[@]}"
			;;
		suse)
			run_as_root zypper install -y "${packages[@]}"
			;;
		*)
			return 1
			;;
	esac
}

ensure_header_include() {
	local file_path="$1"
	local include_line="$2"

	if [[ ! -f "$file_path" ]]; then
		echo "Expected file not found for patching: $file_path" >&2
		exit 1
	fi

	if grep -Fq "$include_line" "$file_path"; then
		return
	fi

	if grep -Eq '^#include <string>$' "$file_path"; then
		sed -i '/^#include <string>$/a\#include <cstdint>' "$file_path"
		return
	fi

	if grep -Eq '^#include <memory>$' "$file_path"; then
		sed -i '/^#include <memory>$/a\#include <cstdint>' "$file_path"
		return
	fi

	echo "Could not apply compatibility patch in: $file_path" >&2
	exit 1
}

apply_vsomeip_compat_patches() {
	local plugin_hpp="$SRC_DIR/interface/vsomeip/plugin.hpp"
	local app_plugin_hpp="$SRC_DIR/interface/vsomeip/plugins/application_plugin.hpp"
	local cmake_lists="$SRC_DIR/CMakeLists.txt"

	ensure_header_include "$plugin_hpp" "#include <cstdint>"
	ensure_header_include "$app_plugin_hpp" "#include <cstdint>"

	if [[ ! -f "$cmake_lists" ]]; then
		echo "Expected file not found for patching: $cmake_lists" >&2
		exit 1
	fi

	# Newer compiler/Boost combinations trigger warnings in transitive headers.
	# vSomeIP enforces -Werror upstream; drop it in local third-party builds.
	sed -i 's/[[:space:]]-Werror//g' "$cmake_lists"
}

detect_boost_dev_package() {
	if [[ -f /etc/os-release ]]; then
		# shellcheck disable=SC1091
		source /etc/os-release
		local id="${ID:-}"
		local id_like="${ID_LIKE:-}"

		case "$id" in
			ubuntu|debian)
				echo "libboost-all-dev"
				return
				;;
			fedora|rhel|centos|rocky|almalinux|opensuse*|sles)
				echo "boost-devel"
				return
				;;
			arch)
				echo "boost"
				return
				;;
		esac

		case " $id_like " in
			*" debian "*)
				echo "libboost-all-dev"
				return
				;;
			*" rhel "*|*" fedora "*|*" suse "*)
				echo "boost-devel"
				return
				;;
			*" arch "*)
				echo "boost"
				return
				;;
		esac
	fi

	echo ""
}

require_command() {
	local cmd="$1"
	if ! command -v "$cmd" >/dev/null 2>&1; then
		echo "Missing required tool: ${cmd}" >&2
		case "$cmd" in
			cmake)
				echo "Hint: $(detect_package_hint cmake)" >&2
				;;
			git)
				echo "Hint: $(detect_package_hint git)" >&2
				;;
			g++)
				echo "Hint: $(detect_package_hint g++)" >&2
				;;
		esac
		exit 1
	fi
}

check_boost_dev() {
	if echo "#include <boost/version.hpp>" | g++ -E -x c++ - >/dev/null 2>&1; then
		return
	fi

	local boost_package
	boost_package="$(detect_boost_dev_package)"

	echo "Missing Boost development headers (required by vsomeip)." >&2
	if [[ -n "$boost_package" ]]; then
		echo "Detected distro from /etc/os-release. Install with:" >&2
		echo "  $(detect_package_hint "$boost_package")" >&2
	else
		echo "Could not detect distro precisely. Install with one of these commands:" >&2
		echo "  Debian/Ubuntu: sudo apt-get install -y libboost-all-dev" >&2
		echo "  Fedora:        sudo dnf install -y boost-devel" >&2
		echo "  RHEL/CentOS:   sudo yum install -y boost-devel" >&2
		echo "  Arch:          sudo pacman -S --noconfirm boost" >&2
		echo "  openSUSE:      sudo zypper install -y boost-devel" >&2
	fi
	exit 1
}

ensure_dependencies() {
	local family
	family="$(detect_distro_family)"
	local -a missing_deps=()
	local -a missing_packages=()
	local -a unique_packages=()
	local install_reply=""
	local package=""
	local install_hint=""

	if ! command -v git >/dev/null 2>&1; then
		missing_deps+=("git")
	fi

	if ! command -v cmake >/dev/null 2>&1; then
		missing_deps+=("cmake")
	fi

	if ! command -v g++ >/dev/null 2>&1; then
		missing_deps+=("g++")
	fi

	if ! echo "#include <boost/version.hpp>" | g++ -E -x c++ - >/dev/null 2>&1; then
		missing_deps+=("boost_dev")
	fi

	if [[ ${#missing_deps[@]} -eq 0 ]]; then
		return
	fi

	if [[ "$family" == "unknown" ]]; then
		echo "Could not detect Linux distro. Install dependencies manually:" >&2
		echo "  git, cmake, g++ and Boost development headers" >&2
		exit 1
	fi

	for dependency in "${missing_deps[@]}"; do
		package="$(package_for_dependency "$family" "$dependency")"
		if [[ -n "$package" ]]; then
			missing_packages+=("$package")
		fi
	done

	declare -A seen_packages=()
	for package in "${missing_packages[@]}"; do
		if [[ -z "${seen_packages[$package]+x}" ]]; then
			unique_packages+=("$package")
			seen_packages[$package]=1
		fi
	done

	install_hint="$(detect_package_hint "${unique_packages[*]}")"
	echo "Missing dependencies: ${unique_packages[*]}" >&2
	echo "Detected distro family: ${family}" >&2
	echo "Install command: ${install_hint}" >&2
	read -r -p "Install missing dependencies now? [Y/n] " install_reply

	if [[ -z "$install_reply" || "$install_reply" =~ ^[Yy]$ ]]; then
		install_packages "$family" "${unique_packages[@]}"
	else
		echo "Dependency installation skipped by user." >&2
		exit 1
	fi

	require_command git
	require_command cmake
	require_command g++
	check_boost_dev
}

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
BUILD_DIR="$ROOT_DIR/build/someip"
SRC_DIR="$BUILD_DIR/src"
INSTALL_DIR="$BUILD_DIR/install-local"
VSOMEIP_REF="${VSOMEIP_REF:-3.4.10}"
VSOMEIP_CMAKE_CXX_FLAGS="${VSOMEIP_CMAKE_CXX_FLAGS:--Wno-error}"

ensure_dependencies

if [[ -f "$INSTALL_DIR/lib/libvsomeip3.so" || -f "$INSTALL_DIR/lib/libvsomeip3.a" ]]; then
	echo "vsomeip already installed into: $INSTALL_DIR"
	exit 0
fi

mkdir -p "$BUILD_DIR"

if [[ ! -d "$SRC_DIR/.git" ]]; then
	git clone --depth 1 --branch "$VSOMEIP_REF" https://github.com/COVESA/vsomeip.git "$SRC_DIR"
else
	git -C "$SRC_DIR" fetch --depth 1 origin "$VSOMEIP_REF"
	git -C "$SRC_DIR" checkout -f "$VSOMEIP_REF"
fi

apply_vsomeip_compat_patches

cmake -S "$SRC_DIR" -B "$BUILD_DIR/build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
	-DCMAKE_CXX_FLAGS="$VSOMEIP_CMAKE_CXX_FLAGS" \
	-DBUILD_SHARED_LIBS=OFF \
	-DVSOMEIP_BUILD_TESTS=OFF \
	-DVSOMEIP_BUILD_EXAMPLES=OFF

cmake --build "$BUILD_DIR/build" -j"$(nproc)"
cmake --install "$BUILD_DIR/build"

echo "vsomeip installed into: $INSTALL_DIR"
