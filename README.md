# ISP Kernel Module source
Kernel Loadable Module for ISP. This module is used to communicate with the ISP hardware.
VVcam sensor kernel modules sensors are in vvcam folder.

## Pre-requisites
### Kernel source
The kernel source is required to build the kernel module. The kernel source can be cloned from [Vishvam Corp Github](https://github.com/VishvamCorp/linux).
Current `isp-vvcam` is tested with kernels 5.15.71 and 6.6.23.

After clone go into the kernel source folder and checkout the needed version. For example, to checkout kernel 5.15.71:

```bash
git checkout cartzy-imx_5.15.y
```

To checkout kernel 6.6.23:
```bash
git checkout cartzy-if-6.6.y
```

More information about kernel configuration can be found in [Boundary Devices](https://boundarydevices.com/customizing-ubuntu-debian-kernels-on-i-mx8-boards/) website.

### Cross compiler
```bash
sudo apt install gcc-arm-linux-gnueabihf
```

### Other tools
```bash
sudo apt install u-boot-tools lzop
```

## Build
Go into kernel source folder and type:
```bash
export KERNEL_SRC=$PWD
export INSTALL_MOD_PATH=$KERNEL_SRC/ubuntunize64/linux-staging
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export CONFIG_CLD_HL_SDIO_CORE=y
```

Then return to current `isp-vvcam` folder and run:

For kernel 5.15.x:
```bash
git switch cartzy-if-5.15.y
```

For kernel 6.6.x:
```bash
git switch cartzy-if-6.6.y
```

Then run:

```bash
./build-all-vvcam.sh
```

Compiled modules will be in `modules` folder.

If you want to install modules into the kernel source folder, run:
```bash
cd vvcam
make modules_install
```

All modules will be installed in `$INSTALL_MOD_PATH` which we defined before.
