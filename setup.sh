#!/bin/bash
set -e

# This script builds and runs the LAN Monitor
# Ensure you are in the project root: ~/LAN/lan-monitor

echo "Building LAN Monitor..."

cd "/home/falcon/LAN/lan-monitor"
mkdir -p /home/falcon/LAN/mybuild
cd /home/falcon/LAN/mybuild
cmake /home/falcon/LAN/lan-monitor -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "Build complete."
echo "Granting raw socket capability (requires sudo once per build)..."
echo "1234" | sudo -S setcap cap_net_raw,cap_net_admin+eip ./LANMonitor

echo "Launching as current user (no sudo — X11 display will work)..."
./LANMonitor
