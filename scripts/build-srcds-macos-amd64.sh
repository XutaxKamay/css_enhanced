#!/bin/sh
BUILD_TYPE=${BUILD_TYPE:-release}
./waf configur --disable-warnse -T $BUILD_TYPE --dedicated "$@" &&
./waf build install
