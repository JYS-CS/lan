#!/usr/bin/env bash
set -e
cd /home/falcon/LAN/mybuild
make -j$(nproc) 2>&1 | tee /tmp/lan_build.log
echo "Build exit: $?"
