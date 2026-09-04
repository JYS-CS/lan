#!/bin/bash

# Simple script to mimic Vite's 'npm run dev' hot-reloading experience
echo "Watching for C++ file changes (Hot Reload Mode)..."
echo "Press Ctrl+C to stop."

# Watches all .cpp and .h files for changes. If a change happens, 
# it quickly switches to the mybuild directory, compiles via make, and restarts!
find . \( -name "*.cpp" -o -name "*.h" \) | entr -r sh -c 'echo "Rebuilding..." && cd /home/falcon/LAN/mybuild && cmake /home/falcon/LAN/lan-monitor -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) && echo "Rebuild complete! Launching app..." && sudo ./LANMonitor'
