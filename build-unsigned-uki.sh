#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2024 Beckhoff Automation GmbH & Co. KG

install_kernel_modules() {
	${MAKE} \
		--file="${srctree}/Makefile" \
		INSTALL_MOD_PATH="${package_dir}" \
		INSTALL_MOD_STRIP=1 \
		modules_install

	rm --force "${package_dir}/lib/modules/${KERNELRELEASE}/build"
}

build_initramfs() {
	dracut \
		--kernel-image="${kernel_image}" \
		--kver="${KERNELRELEASE}" \
		--kmoddir="${package_dir}/lib/modules/${KERNELRELEASE}" \
		--no-hostonly \
		--force \
		--verbose \
		"${initrd_stage_file}"
}

build_unified_kernel_image() {
	mkdir --parents "${uki_install_path}"

	ukify build \
		--cmdline="@${srctree}/cmdline" \
		--initrd="${initrd_stage_file}" \
		--linux="${kernel_image}" \
		--measure \
		--output="${uki_install_path}/${uki_file_name}" \
		--uname="${KERNELRELEASE}"

	rm --force "${initrd_stage_file}"
}

build_uki_addons() {
	mkdir --parents "${uki_addons_path}"

	# Fortunately for us, TcCoreConf only allows sharing the first N cores, so
	# we can just loop from 1 to 63 and generate the addons. All that's left for
	# TcCoreConf to do, is to copy the correct addon to the ESP.
	local _shared_cores
	for _shared_cores in $(seq 1 63); do
		local _irqmax=$((_shared_cores - 1))

		local _irqaffinity
		if test "${_irqmax}" -eq 0; then
			_irqaffinity="0"
		else
			_irqaffinity="0-${_irqmax}"
		fi

		ukify build \
			--cmdline="irqaffinity=${_irqaffinity} isolcpus=${_shared_cores}-N rcu_nocbs=${_shared_cores}-N" \
			--output="${uki_addons_path}/tccoreconf-shared-${_shared_cores}.addon.efi"
	done
}

install_maintainer_scripts() {
	local _host_uki_path="${uki_install_path#"${package_dir}"}/${uki_file_name}"

	mkdir --parents "${package_dir}/DEBIAN"

	tee "${package_dir}/DEBIAN/postinst" <<- EOF
		#!/bin/sh
		set -e

		case "\${1}" in
			configure)
				if bootctl --print-esp-path 2>/dev/null; then
					kernel-install add "${KERNELRELEASE}" "${_host_uki_path}"
				else
					printf 'Could not find the ESP; skipping kernel-install for ${package}.\n' >&2
					printf 'After mounting your ESP (e.g. at /boot/efi), run:\n' >&2
					printf ' sudo dpkg-reconfigure ${package}\n' >&2
				fi
				;;
		esac
	EOF

	chmod 755 "${package_dir}/DEBIAN/postinst"

	tee "${package_dir}/DEBIAN/prerm" <<- EOF
		#!/bin/sh
		set -e

		case "\${1}" in
			remove|deconfigure)
				kernel-install remove "${KERNELRELEASE}"
				;;
		esac
	EOF

	chmod 755 "${package_dir}/DEBIAN/prerm"
}

set -e
set -u

. "$(shlib.sh get-path)/log.sh"

readonly package="${1}"
readonly package_dir="debian/${package}"

readonly initrd_stage_file="${package_dir}/initrd.img"
readonly uki_install_path="${package_dir}/usr/lib/modules/${KERNELRELEASE}"
readonly uki_addons_path="${uki_install_path}/addons"
readonly uki_file_name="vmlinuz.unsigned.efi"

if ! kernel_image="$(${MAKE} --silent --file="${srctree}/Makefile" image_name)"; then
	logerr 'Failed to determine kernel image name.\n'
	exit 1
fi
readonly kernel_image

# 1.) Install the kernel modules.
# They are required both for the finished package, as well as for building the initramfs.
install_kernel_modules

# 2.) Build a dracut initrd, which ends up getting bundled into the UKI.
build_initramfs

# 3.) Build the unified kernel image, using the kernel image and the initrd.
build_unified_kernel_image

# 4.) Build any required UKI addons, which can be used to extend the kernel cmdline.
build_uki_addons

# 5.) Install the maintainer scripts that (re)move the UKI to/from the ESP.
install_maintainer_scripts
