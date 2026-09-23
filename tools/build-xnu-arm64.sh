#!/bin/sh
# Build the XNU kernel for arm64 (BCM2837 board config) with the host's Xcode
# toolchain, bypassing the ravynOS LLVM toolchain build. This is the kernel
# that xnuhv boots under Hypervisor.framework on Apple silicon.
#
# Usage: tools/build-xnu-arm64.sh [make targets...]   (default: all)
# Output: $OBJ/xnu/DEVELOPMENT_ARM64_BCM2837/kernel.development.bcm2837
set -e

R=$(cd "$(dirname "$0")/.." && pwd)
OBJ=${OBJ:-$R/../build-arm64}
mkdir -p "$OBJ/lib" "$OBJ/firehose"
OBJ=$(cd "$OBJ" && pwd)

X=$R/Kernel/xnu
AP=$R/Developer/ravynOS.sdk/usr/local/libexec/availability.pl
export XCRUN=/usr/bin/xcrun

xnu() {
	gmake -C "$X" -j"${J:-$(sysctl -n hw.ncpu)}" SDKROOT=macosx PLATFORM=MacOSX \
	  ARCH_CONFIGS=ARM64 MACHINE_CONFIGS=BCM2837 KERNEL_CONFIGS=DEVELOPMENT \
	  BUILD_WERROR=0 BUILD_LTO=0 DO_CTFMERGE=0 BUILD_PROFILE=0 BUILD_STATIC_LINK=1 \
	  CTFCONVERT=/usr/bin/true CTFMERGE=/usr/bin/true CTFINSERT=/usr/bin/true \
	  AVAILABILITY_PL="$AP" AVAILABILITY_PL_PATH="$AP" \
	  SRCROOT="$X" OBJROOT="$OBJ/xnu" SYMROOT="$OBJ/sym" DSTROOT="$OBJ/dst" \
	  RAVYN_SDKROOT="$R/Developer/ravynOS.sdk" LDFLAGS_KERNEL_SDK="-L$OBJ/lib" \
	  EMBEDDED_DEVICE_MAP=/usr/bin/true ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 \
	  "$@"
}

# libfirehose_kernel needs the kernel's exported headers, and the kernel link
# needs libfirehose_kernel: export headers first, then build the library.
if [ ! -f "$OBJ/lib/libfirehose_kernel.a" ]; then
	xnu exporthdrs
	E=$OBJ/xnu/EXPORT_HDRS
	xcrun clang -arch arm64 -mkernel -c -O2 -nostdinc \
	  -DKERNEL=1 -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE -DARM64_BOARD_CONFIG_BCM2837 \
	  -DDISPATCH_USE_DTRACE=0 -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
	  -DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0 -Wno-implicit-function-declaration \
	  -I"$E/osfmk" -I"$E/bsd" -I"$E/libkern" -I"$E/iokit" -I"$E/pexpert" \
	  -I"$X/libkern" -I"$X/libkern/firehose" -I"$X/EXTERNAL_HEADERS" \
	  -I"$R/Libraries/Libsystem/private" \
	  -I"$R/Developer/ravynOS.sdk/usr/local/include" \
	  -I"$R/Developer/ravynOS.sdk/usr/local/include/kernel" \
	  "$R/Libraries/Libsystem/libdispatch/src/firehose/firehose_buffer.c" \
	  -o "$OBJ/firehose/firehose_buffer.o"
	xcrun libtool -static -o "$OBJ/lib/libfirehose_kernel.a" "$OBJ/firehose/firehose_buffer.o"
fi

[ $# -eq 0 ] && set -- all
xnu "$@"
