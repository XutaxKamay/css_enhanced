#!/bin/sh

git submodule init && git submodule update
sudo apt-get update
sudo apt-get install -y libbz2-dev libzstd-dev

./waf configure -T release --sanitize --disable-warns --tests --prefix=out/ $* &&
./waf install &&
cd out &&
LD_LIBRARY_PATH=bin/ ./unittest
