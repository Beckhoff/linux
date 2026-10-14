ARG REGISTRY_HOST
FROM ${REGISTRY_HOST}/beckhoff/bdpg:v13.3.7

RUN sed --in-place \
	's/^Components: main$/Components: main non-free-firmware/' \
	/etc/apt/sources.list.d/debian.sources

RUN apt-get update && apt-get install --yes \
	bc \
	bison \
	cpio \
	dracut \
	dracut-network \
	firmware-intel-misc \
	flex \
	kmod \
	libdw-dev \
	libelf-dev \
	libssl-dev \
	pahole \
	sbsigntool \
	shlib \
	systemd-cryptsetup \
	systemd-ukify \
	tpm2-tools \
	zstd \
&& rm --force --recursive /var/lib/apt/lists/*
