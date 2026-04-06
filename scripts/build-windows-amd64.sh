#!/bin/sh
BUILD_TYPE=${BUILD_TYPE:-release}
./waf configure -T $BUILD_TYPE --dxvk=false "$@" &&
./waf build install
