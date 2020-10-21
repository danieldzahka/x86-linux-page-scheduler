# Instructions

## OS Distribution
Install Fedora 31 Server. Do not use any other distribution of Linux.

## Kernel
This system uses a non-distribution kernel. To download the kernel
v5.4.18 source code, apply a patch, and copy the configuration file into place run:
```
./get_kernel_5_4_18.sh
```

Install the packages required to build linux, then build the kernel with:
```
cd linux-stable
make
```

Install the kernel with:
```
cd linux-stable
sudo make install
```
## Configuring pmem with ipmctl, ndctl and daxctl
Configure pmem device in app direct mode using `ipmctl`. Create pmem namespace `dax0.0` using the `ndctl` command:
```
ndctl create-namespace -m devdax -s 204G
```
Reconfigure `dax0.0` into system-ram represented by a numa node with `daxctl`:
```
daxctl reconfigure-device --mode=system-ram dax0.0
```
Verify that the pmem numa node was created with `numactl -H`.
## Compiling kernel module, userspace runtime, and rodinia
Run:
```
make
```

## Install the kernel module
```
cd mod
sudo ./install.sh
```

## Run the rodinia programs
```
cd user
./run-suite.sh
```