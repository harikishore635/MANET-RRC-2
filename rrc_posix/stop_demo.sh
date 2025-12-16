#!/bin/bash
# Stop all RRC POSIX demo nodes

echo "Stopping RRC POSIX Integration demo..."

if [ -f .node_pids ]; then
    while read pid; do
        echo "Killing process group $pid..."
        pkill -P $pid 2>/dev/null
        kill $pid 2>/dev/null
    done < .node_pids
    rm -f .node_pids
else
    echo "No PID file found, killing by process name..."
    pkill -f rrc_core
    pkill -f olsr_daemon
    pkill -f tdma_daemon
    pkill -f mac_sim
    pkill -f app_sim
fi

sleep 1

# Cleanup IPC resources
echo "Cleaning up IPC resources..."
rm -f /dev/shm/rrc_* 2>/dev/null
rm -f /dev/mqueue/rrc_* 2>/dev/null

echo "All processes stopped and IPC resources cleaned up."
