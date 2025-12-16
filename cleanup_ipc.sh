#!/bin/bash

echo "Cleaning up RRC IPC resources..."

# Remove message queues
rm -f /dev/mqueue/mq_olsr_to_rrc 2>/dev/null
rm -f /dev/mqueue/mq_rrc_to_olsr 2>/dev/null
rm -f /dev/mqueue/mq_tdma_to_rrc 2>/dev/null
rm -f /dev/mqueue/mq_rrc_to_tdma 2>/dev/null
rm -f /dev/mqueue/mq_phy_to_rrc 2>/dev/null
rm -f /dev/mqueue/mq_rrc_to_phy 2>/dev/null

# Remove shared memory
rm -f /dev/shm/rrc_shared_queues 2>/dev/null
rm -f /dev/shm/rrc_app_shared 2>/dev/null

# Remove semaphores
rm -f /dev/shm/sem.rrc_queue_sem 2>/dev/null
rm -f /dev/shm/sem.rrc_app_sem 2>/dev/null

echo "✓ IPC resources cleaned up"
echo ""
echo "You can now run: ./rrc_integrated"
