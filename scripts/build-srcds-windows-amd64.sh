#!/bin/sh
BUILD_TYPE=${BUILD_TYPE:-release}
./waf configure --disable-warns -T $BUILD_TYPE --dedicated "$@" &&
./waf build install
