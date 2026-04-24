ARG REGISTRY_HOST
FROM ${REGISTRY_HOST}/beckhoff/bdpg:v13.3.7

RUN apt-get update && apt-get install --yes \
	bc \
	bison \
	cpio \
	dracut \
	dracut-network \
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
