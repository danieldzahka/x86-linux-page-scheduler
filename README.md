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