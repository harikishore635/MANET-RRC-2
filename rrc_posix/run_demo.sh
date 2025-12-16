#!/bin/bash
# RRC POSIX Integration - Single Node Demo Script

if [ -z "$1" ]; then
    echo "Usage: $0 <node_id>"
    echo "Example: $0 1"
    exit 1
fi

NODE_ID=$1

echo "========================================"
echo "Starting RRC POSIX Integration"
echo "Node ID: $NODE_ID"
echo "========================================"
echo ""

# Cleanup old IPC resources
if [ "$NODE_ID" = "1" ]; then
    echo "Cleaning up old IPC resources..."
    rm -f /dev/shm/rrc_* 2>/dev/null
    rm -f /dev/mqueue/rrc_* 2>/dev/null
    sleep 1
fi

# Start processes in order
echo "Starting daemons..."

# Start RRC Core
echo "[1/5] Starting RRC Core..."
./rrc_core $NODE_ID &
RRC_PID=$!
sleep 1

# Start OLSR daemon
echo "[2/5] Starting OLSR daemon..."
./olsr_daemon $NODE_ID &
OLSR_PID=$!
sleep 1

# Start TDMA daemon
echo "[3/5] Starting TDMA daemon..."
./tdma_daemon $NODE_ID &
TDMA_PID=$!
sleep 1

# Start MAC simulator
echo "[4/5] Starting MAC simulator..."
./mac_sim $NODE_ID &
MAC_PID=$!
sleep 1

# Start APP simulator
echo "[5/5] Starting APP simulator..."
./app_sim $NODE_ID &
APP_PID=$!

echo ""
echo "========================================"
echo "All processes started for Node $NODE_ID"
echo "========================================"
echo "RRC Core:     PID $RRC_PID"
echo "OLSR daemon:  PID $OLSR_PID"
echo "TDMA daemon:  PID $TDMA_PID"
echo "MAC sim:      PID $MAC_PID"
echo "APP sim:      PID $APP_PID"
echo ""
echo "Press Ctrl+C to stop all processes"
echo "========================================"
echo ""

# Trap Ctrl+C to kill all processes
trap "echo ''; echo 'Stopping all processes...'; kill $RRC_PID $OLSR_PID $TDMA_PID $MAC_PID $APP_PID 2>/dev/null; wait; echo 'All processes stopped'; exit 0" SIGINT SIGTERM

# Wait for all processes
wait
