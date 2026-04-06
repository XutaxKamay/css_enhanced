#!/bin/sh
sudo apt-get update
sudo apt-get install -f -y libopenal-dev g++-multilib gcc-multilib libpng-dev libjpeg-dev libfreetype6-dev libfontconfig1-dev libcurl4-gnutls-dev libsdl2-dev zlib1g-dev libbz2-dev libedit-dev libzstd-dev zip

curl -sL https://github.com/doitsujin/dxvk/releases/download/v2.7.1/dxvk-native-2.7.1-steamrt-sniper.tar.gz -o dxvk-native.tar.gz
sudo tar -xzf dxvk-native.tar.gz -C /
sudo sed -i 's|prefix=/__w/dxvk.*|prefix=/usr|' /usr/lib/pkgconfig/dxvk-d3d9.pc
echo "PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig" >> $GITHUB_ENV
