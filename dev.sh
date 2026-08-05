#!/bin/bash

# Simple script to mimic Vite's 'npm run dev' hot-reloading experience
echo "Watching for C++ file changes (Hot Reload Mode)..."
echo "Press Ctrl+C to stop."

# Watches all .cpp and .h files for changes. If a change happens, 
# it quickly switches to the build directory, compiles via CMake build, and restarts!
find . \( -name "*.cpp" -o -name "*.h" \) | entr -r sh -c 'echo "Rebuilding via CMake..." && cd build && sudo cmake .. && sudo cmake --build . -j$(nproc) && echo "Rebuild complete! Launching app..." && sudo ./LANMonitor'
