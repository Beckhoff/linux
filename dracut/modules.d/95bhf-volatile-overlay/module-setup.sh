#!/bin/bash

check() {
    return 0
}

depends() {
    return 0
}

install() {
    inst_hook pre-pivot 90 "${moddir}/volatile-overlay.sh"
}
