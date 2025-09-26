#!/bin/bash

. /lib/dracut-lib.sh

if ! getargbool 0 bhf.volatile; then
    info "[bhf-volatile-overlay]: skipping volatile rootfs"
    exit 0
fi

info "[bhf-volatile-overlay]: setting up volatile rootfs"
/usr/lib/systemd/systemd-volatile-root overlay /sysroot
