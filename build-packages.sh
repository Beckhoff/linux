#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

usage() {
	logstdin << EOF
USAGE: ${0##*/} <command>
	Build linux kernel packages for GitLab CI.

COMMANDS:
	binary
		Build binary .deb packages and push to pipeline repo.

	source
		Build source package and push to pipeline repo.
EOF
	exit 1
}

prepare() {
	# It seems sometimes there are old build files on the runner
	rm --force ../linux-*.deb

	cp "config-${BHF_CI_ARCH}-${BHF_CI_LINUX_VARIANT}" .config

	# Kernel configs other than our rt one will always lag behind the upstream
	# kernel version, sometimes leading to missing values.
	# To solve this, we follow debian's approach of simply using the linux
	# recommended values via 'make old(def)config' [1].
	#
	# [1]: https://salsa.debian.org/kernel-team/linux/-/blob/1ed079476dab725aaf21ed7ed00d232332a4d01a/debian/rules.real#L166
	make olddefconfig

	# The rt-kernel is special because it ships a localversion-rt file
	# that gets combined with CONFIG_LOCALVERSION.
	# Other kernel variants don't need this, so we rename the file.
	if test "${BHF_CI_LINUX_VARIANT}" != "bhf"; then
		mv localversion-rt .ignoreme.localversion-rt
		"${CLEANUP}/add" "mv .ignoreme.localversion-rt localversion-rt"
	fi
}

make_binary() {
	make -j"$(nproc)" bindeb-pkg

	# GitLab cannot pick up artifacts from outside of CI_PROJECT_DIR
	mv ../linux-*"${CI_PIPELINE_ID}"*.deb ./

	./build-meta-package.sh linux-*.deb
	bdpg push-pipelines ./linux-headers-*.deb ./linux-image-*.deb
}

make_source() {
	make -j"$(nproc)" srcdeb-pkg

	mv ../linux-upstream_*"${CI_PIPELINE_ID}"* .
	bdpg push-pipelines linux-*.dsc
}

set -e
set -u

. "$(shlib.sh get-path)/log.sh"

eval "$(cleanup init)"

export DEBFULLNAME="Beckhoff Automation GmbH & Co. KG"
export DEBEMAIL="info@beckhoff.com"

readonly command="${1:?Missing <command>$(usage)}"

case "${command}" in
	binary)
		prepare
		make_binary
		;;
	source)
		prepare
		make_source
		;;
	*)
		logerr 'Unknown command "%s".\n' "${command}"
		usage
		;;
esac
