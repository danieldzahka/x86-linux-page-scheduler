#!/bin/bash

# Clone kernel source tree
git clone --depth 1 --branch v5.4.18 \
    git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
cp kernel-config linux-stable/.config # copy config file into place
cd linux-stable && make olddefconfig  # configure linux kernel 
