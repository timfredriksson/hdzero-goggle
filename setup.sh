#!/bin/bash
set -e

if [[ ! -d toolchain ]]; then
	echo "downloading toolchain..."
	mkdir toolchain
	wget -O- https://toolchains.bootlin.com/downloads/releases/toolchains/armv7-eabihf/tarballs/armv7-eabihf--musl--stable-2021.11-1.tar.bz2 | tar xj --strip-components=1 -C toolchain
fi

rm -rf build && mkdir build
cmake . -DCMAKE_TOOLCHAIN_FILE=toolchain/share/buildroot/toolchainfile.cmake -Bbuild
