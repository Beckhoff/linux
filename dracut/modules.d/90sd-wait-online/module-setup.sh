#!/bin/bash

check() {
	return 0
}

depends() {
	return 0
}

install() {
	local _dropin_dir="${initdir}/etc/systemd/system/systemd-networkd-wait-online.service.d"

	mkdir --parents "${_dropin_dir}"

	# There is a dracut bug that makes systemd-networkd-wait-online hang, when
	# netbooting over nfs, so we mask it in that case. [1]
	#
	# https://github.com/dracutdevs/dracut/issues/2674
	tee "${_dropin_dir}/override.conf" <<- EOF
		[Unit]
		ConditionKernelCommandLine=!root=/dev/nfs
	EOF
}
