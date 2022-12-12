#!/bin/bash
set -e

if [[ ! -d toolchain ]]; then
	echo "downloading toolchain..."
	mkdir toolchain
	wget -qO- http://musl.cc/arm-linux-musleabihf-cross.tgz | tar xz -C toolchain
fi

rm -rf build && mkdir build
cmake . -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -Bbuild
