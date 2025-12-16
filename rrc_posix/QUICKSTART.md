# RRC POSIX Integration - Quick Reference Guide

## Quick Start (Linux)

```bash
# Build everything
cd rrc_posix
make

# Run single node (Terminal 1)
./run_demo.sh 1

# Run 3-node demo
./run_all_nodes.sh      # All nodes in background
tail -f node1.log       # Watch logs
./stop_demo.sh          # Stop all
```

## Message Flow Cheat Sheet

### APP Sends Data
```
1. APP: app_pool_alloc() → get pool_index
2. APP: Write packet to app_pool[pool_index]
3. APP: Send AppToRrcMsg with pool_index via MQ_APP_TO_RRC
4. RRC: Receives notification, reads from app_pool
5. RRC: Queries OLSR for route
6. RRC: Queries TDMA for slot
7. RRC: Builds frame in frame_pool
8. RRC: Ready for PHY transmission
```

### MAC Delivers Frame
```
1. MAC: Allocate mac_rx_pool entry
2. MAC: Write frame to pool
3. MAC: Send MacToRrcMsg via MQ_MAC_TO_RRC
4. RRC: Check dest_id, allocate frame_pool entry
5. RRC: Send RrcToAppMsg via MQ_RRC_TO_APP
6. APP: Read frame, process, release pool entry
```

### Error Handling
```
1. RRC: Detects error (OLSR/TDMA/timeout)
2. RRC: Build RrcToAppMsg with is_error=1
3. RRC: Send via MQ_RRC_TO_APP
4. APP: Displays error_text
```

## API Quick Reference

### Pool Operations
```c
// Allocate entry
int pool_idx = frame_pool_alloc(&ctx);

// Write to entry
frame_pool_set(&ctx, pool_idx, &frame_data);

// Read from entry
FramePoolEntry* frame = frame_pool_get(&ctx, pool_idx);

// Release entry
frame_pool_release(&ctx, pool_idx);
```

### Message Queue Operations
```c
// Send message
mq_send_msg(&mq_ctx, &msg, sizeof(msg), priority);

// Receive with timeout
mq_recv_msg_timeout(&mq_ctx, &msg, sizeof(msg), &prio, 5000);

// Try receive (non-blocking)
mq_try_recv_msg(&mq_ctx, &msg, sizeof(msg), &prio);
```

### Message Building
```c
// Initialize header
AppToRrcMsg msg;
init_message_header(&msg.header, MSG_APP_TO_RRC_DATA);

// Fill fields
msg.pool_index = pool_idx;
msg.data_type = DATA_TYPE_MSG;
msg.priority = 5;

// Send
mq_send_msg(&mq_app_to_rrc, &msg, sizeof(msg), 5);
```

## Common Commands

```bash
# Check message queues
ls -la /dev/mqueue/rrc_*

# Check shared memory
ls -la /dev/shm/rrc_*

# Manual cleanup
rm -f /dev/mqueue/rrc_*
rm -f /dev/shm/rrc_*

# Kill all processes
pkill -f rrc_core
pkill -f olsr_daemon
pkill -f tdma_daemon
pkill -f mac_sim
pkill -f app_sim
```

## Routing Table (Demo)

```
Node 1:
  dest=2 → next_hop=2 (direct)
  dest=3 → next_hop=2 (via Node 2)

Node 2:
  dest=1 → next_hop=1 (direct)
  dest=3 → next_hop=3 (direct)

Node 3:
  dest=1 → next_hop=2 (via Node 2)
  dest=2 → next_hop=2 (direct)
```

## TDMA Slot Table (Demo)

```
Node 1 → Node 2: slots 0-15
Node 2 → Node 1: slots 0-15
Node 2 → Node 3: slots 16-31
Node 3 → Node 2: slots 16-31
```

## Message Types Summary

| Code | Name | Direction | Purpose |
|------|------|-----------|---------|
| 1 | MSG_APP_TO_RRC_DATA | APP→RRC | Submit packet |
| 2 | MSG_RRC_TO_APP_FRAME | RRC→APP | Deliver frame |
| 3 | MSG_RRC_TO_APP_ERROR | RRC→APP | Report error |
| 10 | MSG_RRC_TO_OLSR_ROUTE_REQ | RRC→OLSR | Route lookup |
| 11 | MSG_OLSR_TO_RRC_ROUTE_RSP | OLSR→RRC | Route response |
| 20 | MSG_RRC_TO_TDMA_SLOT_CHECK | RRC→TDMA | Check slot |
| 22 | MSG_TDMA_TO_RRC_SLOT_RSP | TDMA→RRC | Slot response |
| 30 | MSG_MAC_TO_RRC_RX_FRAME | MAC→RRC | Frame received |

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| 1 | ERROR_OLSR_NO_ROUTE | No route to destination |
| 2 | ERROR_TDMA_SLOT_UNAVAILABLE | No slot available |
| 3 | ERROR_PHY_LINK_POOR | Poor link quality |
| 4 | ERROR_TIMEOUT | Request timeout |
| 5 | ERROR_BUFFER_FULL | Pool exhausted |

## Data Types

| Code | Name | Priority | Use Case |
|------|------|----------|----------|
| 0 | DATA_TYPE_MSG | Normal | Text messages |
| 1 | DATA_TYPE_VOICE | High | Voice calls |
| 2 | DATA_TYPE_VIDEO | Medium | Video streams |
| 3 | DATA_TYPE_FILE | Low | File transfers |
| 4 | DATA_TYPE_RELAY | High | Relay frames |
| 5 | DATA_TYPE_PTT | Highest | PTT voice |
| 99 | DATA_TYPE_UNKNOWN | Lowest | Unknown |

## Debugging Tips

### 1. Queue Not Found
```bash
# Check if RRC core started first
ps aux | grep rrc_core

# Manually create queue (for testing)
cat > /dev/mqueue/rrc_test_mq << EOF
EOF
```

### 2. Pool Full
```c
// Check pool stats
PoolStats stats;
pool_get_stats(&frame_pool, &stats);
printf("In use: %u / %zu\n", stats.in_use_count, FRAME_POOL_SIZE);
```

### 3. Message Timeout
```c
// Reduce timeout for testing
#define REQUEST_TIMEOUT_MS 1000  // 1 second instead of 5
```

### 4. Enable Verbose Logging
```c
// Add to each handler
printf("[DEBUG] Request ID: %u, Timestamp: %u\n", 
       msg.header.request_id, msg.header.timestamp_ms);
```

## Performance Tuning

### Increase Queue Depth
```c
// In mq_init()
ctx->attr.mq_maxmsg = 100;  // Default is 10
```

### Increase Pool Size
```c
#define FRAME_POOL_SIZE 256   // Default is 64
#define APP_POOL_SIZE 128     // Default is 32
```

### Adjust Timeouts
```c
#define REQUEST_TIMEOUT_MS 2000  // Reduce from 5000
```

### Reduce Sleep Intervals
```c
// In main loops
usleep(1000);  // 1ms instead of 10ms
```

## Testing Individual Components

```bash
# Test OLSR only
./rrc_core 1 &
./olsr_daemon 1

# Test TDMA only
./rrc_core 1 &
./tdma_daemon 1

# Test APP→RRC only
./rrc_core 1 &
./olsr_daemon 1 &
./tdma_daemon 1 &
./app_sim 1
```

## Integration with Real RRC

Replace simulators with real implementations:

1. **OLSR**: Replace `olsr_daemon.c` with actual OLSR daemon
2. **TDMA**: Replace `tdma_daemon.c` with real MAC scheduler
3. **PHY**: Replace `mac_sim.c` with SDR/PHY layer
4. **APP**: Replace `app_sim.c` with real application

Keep `rrc_core.c` as-is or integrate into main RRC implementation.

---
**Last Updated**: 2024
