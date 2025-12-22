#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j"$(nproc)" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir -p ${OUTDIR}/rootfs/bin
mkdir -p ${OUTDIR}/rootfs/dev
mkdir -p ${OUTDIR}/rootfs/etc
mkdir -p ${OUTDIR}/rootfs/home
mkdir -p ${OUTDIR}/rootfs/lib
mkdir -p ${OUTDIR}/rootfs/lib64
mkdir -p ${OUTDIR}/rootfs/proc
mkdir -p ${OUTDIR}/rootfs/sbin
mkdir -p ${OUTDIR}/rootfs/sys
mkdir -p ${OUTDIR}/rootfs/tmp
mkdir -p ${OUTDIR}/rootfs/usr
mkdir -p ${OUTDIR}/rootfs/usr/bin
mkdir -p ${OUTDIR}/rootfs/usr/sbin
mkdir -p ${OUTDIR}/rootfs/usr/lib
mkdir -p ${OUTDIR}/rootfs/var
mkdir -p ${OUTDIR}/rootfs/var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone https://github.com/mirror/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox (only on first clone)
    make distclean
    make defconfig
    # Enable bash applet with ash backend
    sed -i 's/# CONFIG_BASH_IS_ASH is not set/CONFIG_BASH_IS_ASH=y/' .config
    sed -i 's/CONFIG_BASH_IS_HUSH=y/# CONFIG_BASH_IS_HUSH is not set/' .config
    sed -i 's/CONFIG_BASH_IS_NONE=y/# CONFIG_BASH_IS_NONE is not set/' .config
    # Auto-select ash as the shell (yes to all prompts)
    yes "" | make oldconfig
else
    cd busybox
fi

# Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install CONFIG_PREFIX=${OUTDIR}/rootfs

# Create bash symlink to busybox
ln -sf /bin/busybox ${OUTDIR}/rootfs/bin/bash

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

# Copy program interpreter
cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib/

# Copy shared libraries
cp ${SYSROOT}/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64/
cp ${SYSROOT}/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64/
cp ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64/

# Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 622 ${OUTDIR}/rootfs/dev/console c 5 1

# Clean and build the writer utility
echo "Building the writer utility"
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Verify writer was built
if [ ! -f ${FINDER_APP_DIR}/writer ]; then
    echo "Error: writer utility not found after build"
    exit 1
fi

# Copy the finder related scripts and executables to the / directory
# on the target rootfs
echo "Copying finder scripts and executables to rootfs"
echo "FINDER_APP_DIR is ${FINDER_APP_DIR}"
cp -rL ${FINDER_APP_DIR}/conf ${OUTDIR}/rootfs/
cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/
cp ${FINDER_APP_DIR}/manual-linux.sh ${OUTDIR}/rootfs/
cp ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/

# Chown the root directory
cd ${OUTDIR}/rootfs
sudo chown -R root:root *

# Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio
echo "initramfs.cpio.gz created at ${OUTDIR}/initramfs.cpio.gz"
