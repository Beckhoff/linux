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
	cp --recursive \
		"${srctree}/dracut/modules.d/"* \
		"/usr/lib/dracut/modules.d"

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

	mkdir --parents "${package_dir}/boot"
	cp System.map "${package_dir}/boot/System.map-${KERNELRELEASE}"
	cp "${KCONFIG_CONFIG}" "${package_dir}/boot/config-${KERNELRELEASE}"
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

	# Overlay rootfs used for the USB installer or any sort of Unified Write Filter.
	ukify build \
		--cmdline="bhf.volatile=1" \
		--output="${uki_addons_path}/systemd-volatile.addon.efi"
}

install_maintainer_scripts() {
	local _host_uki_path="${uki_install_path#"${package_dir}"}/${uki_file_name}"

	mkdir --parents "${package_dir}/DEBIAN"

	process_template() {
		sed \
			-e "s|@KERNELRELEASE@|${KERNELRELEASE}|g" \
			-e "s|@HOST_UKI_PATH@|${_host_uki_path}|g" \
			-e "s|@PACKAGE@|${package}|g" \
			"${1}" > "${2}"
		chmod 755 "${2}"
	}

	process_template "${srctree}/scripts/package/debian-uki/postinst" "${package_dir}/DEBIAN/postinst"
	process_template "${srctree}/scripts/package/debian-uki/prerm" "${package_dir}/DEBIAN/prerm"
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
