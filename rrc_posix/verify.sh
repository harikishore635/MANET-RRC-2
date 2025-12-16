#!/bin/bash
# RRC POSIX Integration - Build and Test Verification Script

set -e  # Exit on error

echo "========================================"
echo "RRC POSIX Integration"
echo "Build and Test Verification"
echo "========================================"
echo ""

# Check if running on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "ERROR: This package requires Linux (POSIX message queues)"
    echo "Current OS: $OSTYPE"
    exit 1
fi

# Check for required tools
echo "[1/7] Checking build dependencies..."
command -v gcc >/dev/null 2>&1 || { echo "ERROR: gcc not found"; exit 1; }
command -v make >/dev/null 2>&1 || { echo "ERROR: make not found"; exit 1; }
echo "✓ gcc and make found"

# Check for POSIX message queue support
echo "[2/7] Checking POSIX message queue support..."
if [ ! -d "/dev/mqueue" ]; then
    echo "ERROR: /dev/mqueue not found. POSIX message queues not supported."
    exit 1
fi
echo "✓ POSIX message queues supported"

# Clean any old resources
echo "[3/7] Cleaning old IPC resources..."
rm -f /dev/shm/rrc_* 2>/dev/null || true
rm -f /dev/mqueue/rrc_* 2>/dev/null || true
make clean 2>/dev/null || true
echo "✓ Cleanup complete"

# Build all targets
echo "[4/7] Building all targets..."
make
echo "✓ Build successful"

# Verify all executables
echo "[5/7] Verifying executables..."
EXES="rrc_core olsr_daemon tdma_daemon mac_sim app_sim"
for exe in $EXES; do
    if [ ! -f "$exe" ]; then
        echo "ERROR: $exe not found"
        exit 1
    fi
    if [ ! -x "$exe" ]; then
        echo "ERROR: $exe not executable"
        exit 1
    fi
    echo "  ✓ $exe"
done
echo "✓ All executables verified"

# Make scripts executable
echo "[6/7] Making scripts executable..."
chmod +x run_demo.sh run_all_nodes.sh stop_demo.sh verify.sh
echo "✓ Scripts made executable"

# Quick functional test
echo "[7/7] Running quick functional test..."
echo "  Starting RRC core..."
./rrc_core 1 > /tmp/rrc_test.log 2>&1 &
RRC_PID=$!
sleep 2

if ! ps -p $RRC_PID > /dev/null; then
    echo "ERROR: RRC core failed to start"
    cat /tmp/rrc_test.log
    exit 1
fi

echo "  Checking IPC resources..."
MQ_COUNT=$(ls /dev/mqueue/rrc_* 2>/dev/null | wc -l)
SHM_COUNT=$(ls /dev/shm/rrc_* 2>/dev/null | wc -l)

if [ "$MQ_COUNT" -lt 5 ]; then
    echo "ERROR: Expected at least 5 message queues, found $MQ_COUNT"
    kill $RRC_PID 2>/dev/null
    exit 1
fi

if [ "$SHM_COUNT" -lt 3 ]; then
    echo "ERROR: Expected 3 shared memory regions, found $SHM_COUNT"
    kill $RRC_PID 2>/dev/null
    exit 1
fi

echo "  ✓ Found $MQ_COUNT message queues"
echo "  ✓ Found $SHM_COUNT shared memory regions"

echo "  Stopping RRC core..."
kill $RRC_PID 2>/dev/null
wait $RRC_PID 2>/dev/null || true
sleep 1

echo "✓ Functional test passed"

# Final cleanup
rm -f /tmp/rrc_test.log
rm -f /dev/shm/rrc_* 2>/dev/null || true
rm -f /dev/mqueue/rrc_* 2>/dev/null || true

echo ""
echo "========================================"
echo "✅ VERIFICATION COMPLETE"
echo "========================================"
echo ""
echo "Build Status:     SUCCESS"
echo "Executables:      5 verified"
echo "Message Queues:   Working"
echo "Shared Memory:    Working"
echo ""
echo "Next Steps:"
echo "  1. Run single node:  ./run_demo.sh 1"
echo "  2. Run 3-node demo:  ./run_all_nodes.sh"
echo "  3. Read docs:        cat README.md"
echo ""
echo "========================================"

exit 0
