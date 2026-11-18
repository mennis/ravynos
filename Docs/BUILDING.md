# Building ravynOS From Source

This guide is for the current Darwin-based ravynOS, which builds with BSD make (bmake) from the top-level `Makefile`. A new CMake-based build system is being created for portability and maintainability, but it is not in the tree yet. Structurally, the build starts by creating a toolchain for the host OS and then using it to build the OS for the same CPU architecture as the host: x86_64 on an Intel host, aarch64 on Apple silicon or an arm64 Linux host.

The build writes everything to a directory named `build` next to the source directory (`/path/to/build` for a source tree at `/path/to/ravynos`).

The host toolchain (Default.xctoolchain) includes clang, LLVM, Apple cctools, xcbuild, and other utilities needed to build the OS. Building LLVM initially takes a __long__ time, so be patient. It lives in `<build_dir>/Developer/Toolchains/Default.xctoolchain` once built.

After the toolchain is available, critical system libraries and headers will be built into the ravynOS.sdk - an equivalent of MacOSX.sdk for ravynOS native targets. The new SDK is then used to progressively build the rest of the system.

Building it this way takes longer, but there are advantages. It helps make the entire build portable and it guarantees build consistency, which helps reduce those nasty "well it works for me" problems. Once it has built fully, subsequent incremental builds are faster.

## What you need:
* A host machine with:
  * clang 17.x (Apple, Linux) or 18.x (Linux)
  * cmake 3.15+ and ninja
  * GNU make (gmake)
  * BSD make (bmake)
  * Python 3
  * OpenSSL
  * libpng (for xcbuild), libxml2, zlib (libz)
* A lot of patience


__Running the build:__

From the top of the source tree, run:

    bmake -m $PWD/BSD/share/mk NO_ROOT=1 world

* `-m $PWD/BSD/share/mk` points bmake at ravynOS's own `.mk` files. Without it, bmake stops at once with `Could not find bsd.mkopt.mk`.
* `NO_ROOT=1` skips the install steps that `chown` files to root. Leave it off only if you build as root.
* Add `-j N` to run N jobs in parallel.


__On macOS:__
* Have a recent Xcode installed
* Run the build as above


__On Linux (tested on Arch 2025/12/21):__
* Install `csh`, `bison`, `binutils`, `flex`, `libdispatch`, the LLVM linker `lld`, and `acl` (xar needs its `sys/acl.h`; on Debian and Ubuntu the package is `libacl1-dev`)
* Ubuntu 24.04 has no libdispatch package. Build it from Swift's sources and install it into `/usr/local`, where the build finds it without extra flags:

      git clone --depth 1 https://github.com/swiftlang/swift-corelibs-libdispatch.git
      cmake -S swift-corelibs-libdispatch -B dispatch-build -G Ninja \
          -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
          -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
      ninja -C dispatch-build
      sudo ninja -C dispatch-build install
      sudo ldconfig
* Use clang, not gcc. Either symlink `cc` and `c++` to clang (`rm -f /usr/bin/cc /usr/bin/c++; ln -sf clang /usr/bin/cc; ln -sf clang++ /usr/bin/c++`), or name the compilers on the bmake command line. For example, with Ubuntu's versioned clang 18:

      bmake -m $PWD/BSD/share/mk NO_ROOT=1 world CC=clang-18 CXX=clang++-18 HOST_CC=clang-18 HOST_CXX=clang++-18 HOST_AR=llvm-ar-18
* Run the build as above
