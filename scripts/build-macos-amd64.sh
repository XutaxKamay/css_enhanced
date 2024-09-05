#!/bin/sh

git submodule init && git submodule update

brew install sdl2
brew install zstd

./waf configure -T debug --disable-warns $* &&
./waf build
