#!/bin/sh

git submodule init && git submodule update

brew install sdl2
brew install zstd
brew install zip

./waf configure -T debug --disable-warns $* &&
./waf build
