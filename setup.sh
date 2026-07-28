#!/bin/bash
set -e

# This script builds and runs the LAN Monitor Refactor (Step 3: Clean Architecture)
# Ensure you are in the project root: ~/LAN/lan-monitor

echo "Building LAN Monitor Step 3 Refactor..."

cd "/home/falcon/LAN/lan-monitor"
mkdir -p build
rm -rf build/CMakeCache.txt build/CMakeFiles
cd build
cmake -G Ninja ..
ninja

echo "Build complete."
echo "Granting raw socket capability (requires sudo once per build)..."
echo "1234" | sudo -S setcap cap_net_raw,cap_net_admin+eip ./LANMonitor

echo "Launching as current user (no sudo — X11 display will work)..."
./LANMonitor
