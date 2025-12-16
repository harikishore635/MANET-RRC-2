#!/bin/bash
# RRC POSIX Integration - Run All Nodes Demo

echo "========================================"
echo "RRC POSIX Integration - Multi-Node Demo"
echo "========================================"
echo ""
echo "This script will start all 3 nodes in the background."
echo "View logs in separate terminals:"
echo "  tail -f node1.log"
echo "  tail -f node2.log"
echo "  tail -f node3.log"
echo ""

# Cleanup old IPC resources
echo "Cleaning up old IPC resources..."
rm -f /dev/shm/rrc_* 2>/dev/null
rm -f /dev/mqueue/rrc_* 2>/dev/null
rm -f node*.log 2>/dev/null
sleep 1

# Start Node 1
echo "Starting Node 1..."
./run_demo.sh 1 > node1.log 2>&1 &
NODE1_PID=$!
sleep 2

# Start Node 2
echo "Starting Node 2..."
./run_demo.sh 2 > node2.log 2>&1 &
NODE2_PID=$!
sleep 2

# Start Node 3
echo "Starting Node 3..."
./run_demo.sh 3 > node3.log 2>&1 &
NODE3_PID=$!
sleep 2

echo ""
echo "========================================"
echo "All nodes started"
echo "========================================"
echo "Node 1: PID $NODE1_PID (log: node1.log)"
echo "Node 2: PID $NODE2_PID (log: node2.log)"
echo "Node 3: PID $NODE3_PID (log: node3.log)"
echo ""
echo "Monitor logs:"
echo "  tail -f node1.log"
echo "  tail -f node2.log"
echo "  tail -f node3.log"
echo ""
echo "Stop all nodes:"
echo "  ./stop_demo.sh"
echo ""
echo "========================================"

# Save PIDs for stop script
echo "$NODE1_PID" > .node_pids
echo "$NODE2_PID" >> .node_pids
echo "$NODE3_PID" >> .node_pids

# Wait for user to press Ctrl+C
trap "echo ''; echo 'Stopping all nodes...'; kill $NODE1_PID $NODE2_PID $NODE3_PID 2>/dev/null; rm -f .node_pids; echo 'All nodes stopped'; exit 0" SIGINT SIGTERM

echo "Press Ctrl+C to stop all nodes..."
wait
