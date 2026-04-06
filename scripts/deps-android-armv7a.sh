#!/bin/sh
sudo apt-get update
sudo apt-get install -y zip wget unzip

wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export NDK_HOME=$PWD/android-ndk-r10e/

if [ -n "$GITHUB_ENV" ]; then
  echo "ANDROID_NDK_HOME=$PWD/android-ndk-r10e/" >> "$GITHUB_ENV"
  echo "NDK_HOME=$PWD/android-ndk-r10e/" >> "$GITHUB_ENV"
fi
