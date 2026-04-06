#!/bin/sh
BUILD_TYPE=${BUILD_TYPE:-release}
./waf configure -T $BUILD_TYPE --dedicated "$@" &&
./waf build install
