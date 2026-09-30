#!/bin/bash

if ! getargbool 0 bhf.volatile; then
	info "[bhf-volatile-overlay]: skipping volatile rootfs"
	return 0
fi

# systemd-networkd keeps the initrd's IP configuration (KeepConfiguration=yes)
# only if / is a network filesystem [1]. With the volatile overlay, / is an
# overlayfs, so networkd would drop the IPs our NFS root depends on.
#
# networkd also treats / as a network filesystem if it has the _netdev mount
# option [2]. _netdev is a userspace-only option that libmount keeps in
# /run/mount/utab and matches to a mount by source, target and root. 
#
# We add the entry for where the overlay ends up after switch-root, since
# switch-root moves /sysroot to / without updating utab. /run survives
# switch-root, so networkd sees _netdev on / when it starts.
#
# [1] https://salsa.debian.org/systemd-team/systemd/-/blob/f02a1990fdc9417061c1f690fa0fb6179bcaa7f4/src/network/networkd-manager.c#L516-L527
# [2] https://salsa.debian.org/systemd-team/systemd/-/blob/f02a1990fdc9417061c1f690fa0fb6179bcaa7f4/src/shared/mount-util.c#L1872-L1898
if findmnt --types=nfs,nfs4 --mountpoint=/sysroot > /dev/null; then
	mkdir --parents /run/mount
	printf 'SRC=overlay TARGET=/ ROOT=/ OPTS=_netdev\n' >> /run/mount/utab
fi

info "[bhf-volatile-overlay]: setting up volatile rootfs"
/usr/lib/systemd/systemd-volatile-root overlay /sysroot
