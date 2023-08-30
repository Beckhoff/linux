#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2024 Beckhoff Automation GmbH & Co. KG

get_package_arch() {
	local _filename="${1}"

	# First we drop everthing before the last underscore
	# arm64.deb
	local _suffix="${_filename##*_}"

	# Now, we drop the fileextension:
	# arm64
	printf '%s' "${_suffix%.deb}"
}

build_meta_package() {
	local _package="${1}"
	case "${_package}" in
		linux-headers-*.deb)
			local _meta_package='linux-headers-bhf'
			local _suffix=' headers';;
		linux-image-*-dbg*.deb)
			local _meta_package='linux-image-bhf-dbg'
			local _suffix=' debugging symbols';;
		linux-image-*.deb)
			local _meta_package='linux-image-bhf'
			local _suffix='';;
		linux-libc-dev*.deb)
			printf 'WARNING: linux-libc-dev not supported, ignoring "%s".\n' "${_package}" >&2
			return;;
		*)
			printf 'ERROR: Unknown package type "%s".\n' "${_package}" >&2
			exit 1
	esac

	local _debarch
	_debarch="$(get_package_arch "${_package}")"

	equivs-build - <<- EOF
		Section: kernel
		Priority: optional
		Standards-Version: 4.1.4
		Version: ${CI_PIPELINE_ID}
		Maintainer: Beckhoff Automation GmbH & Co. KG <info@beckhoff.com>

		Package: ${_meta_package}
		Architecture: ${_debarch}
		Depends: ${_package%%_*}
		Description: Meta package for linux kernel${_suffix}
		 This package is a meta package pointing to the latest linux kernel${_suffix}.
EOF
}

set -e
set -u

for _package in "$@"; do
	build_meta_package "${_package}"
done
