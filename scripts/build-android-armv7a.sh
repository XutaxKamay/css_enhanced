#!/bin/sh
BUILD_TYPE=${BUILD_TYPE:-release}
./waf configure --disable-warns --togles -T $BUILD_TYPE --android=armeabi-v7a-hard,4.9,21 "$@" &&
./waf build install
