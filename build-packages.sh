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

	cp "config-${BHF_CI_ARCH}" .config
}

make_binary() {
	make -j"$(nproc)" bindeb-pkg

	# GitLab cannot pick up artifacts from outside of CI_PROJECT_DIR
	mv ../linux-*"${CI_PIPELINE_ID}"*.deb ./

	./build-meta-package.sh linux-*.deb
	bdpg push-pipelines linux-*.deb
}

make_source() {
	make -j"$(nproc)" srcdeb-pkg

	mv ../linux-upstream_*"${CI_PIPELINE_ID}"* .
	bdpg push-pipelines linux-*.dsc
}

set -e
set -u

. "$(shlib.sh get-path)/log.sh"

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
