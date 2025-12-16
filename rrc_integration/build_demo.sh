#!/bin/bash
# RRC Message Queue Demo - Build Script
# For Linux/macOS

echo "========================================"
echo "RRC Message Queue System - Build Script"
echo "========================================"
echo ""

# Check if gcc is available
echo "Checking for GCC compiler..."
if ! command -v gcc &> /dev/null; then
    echo "ERROR: GCC compiler not found!"
    echo "Please install GCC: sudo apt-get install build-essential (Ubuntu/Debian)"
    exit 1
fi
echo "GCC found: $(which gcc)"
echo ""

# Build the demo
echo "Building RRC Message Queue Demo..."
echo "Compiling: rrc_message_queue.c rrc_api_wrappers.c olsr_thread.c tdma_thread.c phy_thread.c demo_threads.c"

gcc -o rrc_demo \
    rrc_message_queue.c \
    rrc_api_wrappers.c \
    olsr_thread.c \
    tdma_thread.c \
    phy_thread.c \
    demo_threads.c \
    -lpthread -Wall

if [ $? -eq 0 ] && [ -f rrc_demo ]; then
    echo ""
    echo "Build successful!"
    echo ""
    echo "Executable: rrc_demo"
    echo ""
    echo "To run the demo:"
    echo "  ./rrc_demo"
    echo ""
    chmod +x rrc_demo
else
    echo ""
    echo "Build failed!"
    exit 1
fi

echo "========================================"
