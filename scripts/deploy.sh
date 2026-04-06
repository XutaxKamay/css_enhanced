#!/bin/sh
# Usage: scripts/deploy.sh <output_zip_name>
# Requires: SSH_KEY env var

cd ./gamedata/css_enhanced/game

if command -v 7z > /dev/null; then
  7z a "$1" ./
else
  zip -r "$1" ./
fi

mkdir -p ~/.ssh
eval $(ssh-agent)
ssh-add - <<< "$SSH_KEY"
ssh-keyscan cssserv.xutaxkamay.com >> ~/.ssh/known_hosts
scp "$1" root@cssserv.xutaxkamay.com:/var/www/html
