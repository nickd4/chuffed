#!/bin/sh
rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake install_manifest.txt
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=on -DCMAKE_INSTALL_PREFIX="$HOME"
# -DBUILD_STATIC_EXECUTABLE=on
make -j3 && make install
