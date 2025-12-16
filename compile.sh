#!/bin/bash

# RRC Integration Compilation Script for Linux/WSL

echo "=========================================="
echo "Compiling RRC Integrated with POSIX IPC"
echo "=========================================="

# Check if gcc is installed
if ! command -v gcc &> /dev/null; then
    echo "ERROR: gcc not found. Please install it:"
    echo "  Ubuntu/Debian: sudo apt-get update && sudo apt-get install build-essential"
    echo "  Fedora/RHEL:   sudo dnf install gcc"
    echo "  Arch:          sudo pacman -S gcc"
    exit 1
fi

# Compile with all warnings and pthread/rt libraries
echo "Compiling rrc_integrated.c..."
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -Wall -Wextra -g

if [ $? -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "✓ Compilation successful!"
    echo "=========================================="
    echo ""
    echo "To run:"
    echo "  ./rrc_integrated        # Default node ID = 1"
    echo "  ./rrc_integrated 5      # Specific node ID = 5"
    echo ""
    echo "Executable: rrc_integrated"
    echo "Size: $(ls -lh rrc_integrated | awk '{print $5}')"
    echo "=========================================="
else
    echo ""
    echo "=========================================="
    echo "✗ Compilation failed!"
    echo "=========================================="
    exit 1
fi
