# RRC Integration - Quick Start Guide

## Compilation

### Option 1: Basic Compilation
```bash
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -Wall -Wextra
```

### Option 2: With Debug Symbols
```bash
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -g -O0 -Wall -Wextra
```

### Option 3: Optimized Release Build
```bash
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -O2 -Wall -Wextra -DNDEBUG
```

### Compiler Flags Explained
- `-pthread`: Enable POSIX threads support
- `-lrt`: Link POSIX real-time library (for mqueue, shm, semaphores)
- `-Wall`: Enable all warnings
- `-Wextra`: Enable extra warnings
- `-g`: Include debug symbols
- `-O0`: No optimization (for debugging)
- `-O2`: Level 2 optimization (for production)
- `-DNDEBUG`: Disable assertions (for production)

---

## Running

### Start Single Node
```bash
# Default node ID = 1
./rrc_integrated

# Specific node ID
./rrc_integrated 5
```

### Expected Startup Sequence
```
1. IPC initialization
2. Subsystem initialization
3. Power ON event (FSM → IDLE)
4. Thread startup (5 threads)
5. Loopback test execution
6. Enter operational mode
```

### Graceful Shutdown
```bash
# Press Ctrl+C
^C

# System will:
# 1. Stop all threads
# 2. Power off FSM
# 3. Cleanup IPC resources
# 4. Exit cleanly
```

---

## Loopback Test Results

When run, the loopback test will output:

```
===========================================
RRC LOOPBACK TEST MODE
===========================================
Testing complete RRC integration with simulated events

>>> Loopback: Simulating PHY metrics update
>>> Loopback: PHY metrics injected for node 3
RRC-PHY: Metrics update for node 3 (RSSI:-65.5, SNR:25.0, PER:1.5%)

>>> Loopback: Simulating OLSR route update
>>> Loopback: Route update injected (node 5 via node 3)
RRC-OLSR: Received route update for node 5 → next hop 3

>>> Loopback: Simulating application downlink packet
>>> Loopback: Application packet injected to app_to_rrc queue
RRC-APP: Received packet from app (src:1 → dest:5, size:100, type:4)
RRC-APP: Initiating connection setup for destination 5
RRC-APP: Route found - next hop is 3

>>> Loopback: Simulating TDMA uplink reception
>>> Loopback: Uplink frame injected to rx_queue with notification
RRC-TDMA: RX queue notification - 1 frames from node 5
RRC-TDMA: Processing uplink frame from node 5 to node 1
RRC-TDMA: Frame is for us, delivering to application
RRC-TDMA: Uplink packet delivered to application

>>> Loopback: RRC to App queue has 1 messages
>>> Loopback: Successfully retrieved uplink packet from rrc_to_app queue
    Source: 5, Dest: 1, Size: 50

===========================================
LOOPBACK TEST COMPLETED
===========================================
```

### What the Test Validates
✅ PHY → RRC message path (metrics)
✅ OLSR → RRC message path (routes)
✅ App → RRC → Queue path (downlink)
✅ TDMA → RRC → App path (uplink)
✅ End-to-end packet delivery
✅ All threads operational
✅ IPC channels working
✅ Shared memory access
✅ Thread-safe queue operations

---

## Monitoring

### Real-Time Statistics (Every 30 seconds)

```
=== RRC FSM Statistics ===
Current state: RRC_STATE_IDLE
State transitions: 2
Setup success: 1
Setup failures: 0
Reconfigurations: 0
Inactivity timeouts: 0
Releases: 0
Power events: 1 on, 0 off

Active connections:
  No active connections
==========================

=== NC Slot Queue Statistics ===
Messages enqueued: 5
Messages transmitted: 3
Queue full events: 0
Current depth: 2
================================

=== Relay Queue Statistics ===
Packets relayed: 10
TTL expirations: 2
Queue full events: 0
Active relays: 1
================================

=== App-RRC Queue Statistics ===
App → RRC: 15 messages
RRC → App: 12 messages
App→RRC overflows: 0
RRC→App overflows: 0
==================================

=== RRC Statistics ===
Packets processed: 42
Messages enqueued: 38
Messages discarded: 0
Route queries: 15
Poor links detected: 1
=====================
```

---

## Debugging

### Check IPC Resources

#### Message Queues
```bash
# List POSIX message queues
ls -la /dev/mqueue/

# Expected queues:
# /dev/mqueue/rrc_to_olsr
# /dev/mqueue/olsr_to_rrc
# /dev/mqueue/rrc_to_tdma
# /dev/mqueue/tdma_to_rrc
# /dev/mqueue/rrc_to_phy
# /dev/mqueue/phy_to_rrc

# Check queue properties
cat /dev/mqueue/rrc_to_olsr
```

#### Shared Memory
```bash
# List POSIX shared memory
ls -la /dev/shm/

# Expected:
# /dev/shm/rrc_shared_queues
# /dev/shm/rrc_app_shared
```

#### Semaphores
```bash
# List POSIX semaphores
ls -la /dev/shm/sem.*

# Expected:
# sem.rrc_shared_mem_sem
# sem.rrc_app_rrc_sem
```

### Cleanup Stale Resources

If previous run didn't clean up properly:

```bash
# Remove message queues
rm -f /dev/mqueue/rrc_to_olsr
rm -f /dev/mqueue/olsr_to_rrc
rm -f /dev/mqueue/rrc_to_tdma
rm -f /dev/mqueue/tdma_to_rrc
rm -f /dev/mqueue/rrc_to_phy
rm -f /dev/mqueue/phy_to_rrc

# Remove shared memory
rm -f /dev/shm/rrc_shared_queues
rm -f /dev/shm/rrc_app_shared

# Remove semaphores
rm -f /dev/shm/sem.rrc_shared_mem_sem
rm -f /dev/shm/sem.rrc_app_rrc_sem
```

### Common Issues

#### Issue 1: "Permission denied" on IPC resources
**Solution**: Run with sufficient permissions or change ownership
```bash
sudo chown $USER /dev/mqueue/rrc_*
sudo chown $USER /dev/shm/rrc_*
```

#### Issue 2: "Resource exists" on startup
**Solution**: Cleanup stale resources (see above)

#### Issue 3: Threads not starting
**Symptom**: "pthread_create" errors
**Solution**: 
- Check ulimit for thread count: `ulimit -u`
- Increase if needed: `ulimit -u 4096`

#### Issue 4: Segmentation fault
**Symptom**: Crash on startup
**Possible Causes**:
1. Shared memory not initialized
2. NULL pointer dereference
3. Stack overflow

**Debug**:
```bash
# Run with gdb
gdb ./rrc_integrated
(gdb) run 1
(gdb) bt  # If crash occurs
```

#### Issue 5: High CPU usage
**Symptom**: 100% CPU usage
**Cause**: Tight polling loop in threads
**Solution**: Threads already have 1ms sleep - verify it's present

---

## Integration with Other Layers

### OLSR Layer Integration

**What OLSR needs to do**:
1. Open message queues:
   - Read from `/dev/mqueue/rrc_to_olsr`
   - Write to `/dev/mqueue/olsr_to_rrc`
2. Send route responses when queried
3. Forward OLSR protocol messages to RRC for NC broadcast

**Example OLSR Integration**:
```c
// In OLSR process
mqd_t olsr_from_rrc = mq_open("/rrc_to_olsr", O_RDONLY);
mqd_t olsr_to_rrc = mq_open("/olsr_to_rrc", O_WRONLY);

// Listen for route queries
IPC_Message msg;
while (1) {
    if (mq_receive(olsr_from_rrc, (char*)&msg, sizeof(IPC_Message), NULL) > 0) {
        if (msg.type == MSG_RRC_ROUTE_QUERY) {
            // Lookup route
            uint8_t next_hop = get_next_hop(msg.route_query.dest_node);
            
            // Send response
            IPC_Message response;
            response.type = MSG_OLSR_ROUTE_RESPONSE;
            response.route_response.dest_node = msg.route_query.dest_node;
            response.route_response.next_hop = next_hop;
            response.route_response.hop_count = get_hop_count(msg.route_query.dest_node);
            response.route_response.route_available = (next_hop != 0xFF);
            
            mq_send(olsr_to_rrc, (char*)&response, sizeof(IPC_Message), 0);
        }
    }
}
```

### TDMA Layer Integration

**What TDMA needs to do**:
1. Open message queues:
   - Read from `/dev/mqueue/rrc_to_tdma`
   - Write to `/dev/mqueue/tdma_to_rrc`
2. Open shared memory:
   - `/dev/shm/rrc_shared_queues`
3. Read from downlink queues (analog_voice_queue, data_from_l3_queue[])
4. Write received frames to rx_queue
5. Send notifications when frames received

**Example TDMA Integration**:
```c
// In TDMA process
mqd_t tdma_from_rrc = mq_open("/rrc_to_tdma", O_RDONLY);
mqd_t tdma_to_rrc = mq_open("/tdma_to_rrc", O_WRONLY);

// Map shared memory
int shm_fd = shm_open("rrc_shared_queues", O_RDWR, 0666);
SharedQueueMemory *queues = mmap(NULL, sizeof(SharedQueueMemory), 
                                  PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

// Open semaphore
sem_t *sem = sem_open("rrc_shared_mem_sem", 0);

// Transmit from downlink queues
while (1) {
    sem_wait(sem);
    if (!is_queue_empty(&queues->analog_voice_queue)) {
        struct frame tx_frame = dequeue_shared(&queues->analog_voice_queue);
        // Transmit tx_frame over PHY
        transmit(tx_frame);
    }
    sem_post(sem);
}

// When frame received
void on_frame_received(struct frame rx_frame) {
    sem_wait(sem);
    enqueue_shared(&queues->rx_queue, rx_frame);
    sem_post(sem);
    
    // Notify RRC
    IPC_Message notify;
    notify.type = MSG_TDMA_RX_QUEUE_DATA;
    notify.rx_notify.frame_count = 1;
    notify.rx_notify.source_node = rx_frame.source_add;
    mq_send(tdma_to_rrc, (char*)&notify, sizeof(IPC_Message), 0);
}
```

### Application Layer Integration

**What App needs to do**:
1. Open shared memory: `/dev/shm/rrc_app_shared`
2. Open semaphore: `rrc_app_rrc_sem`
3. Write to `app_to_rrc_queue` for downlink
4. Read from `rrc_to_app_queue` for uplink

**Example App Integration**:
```c
// In Application process
int shm_fd = shm_open("rrc_app_shared", O_RDWR, 0666);
AppRRCSharedMemory *app_rrc = mmap(NULL, sizeof(AppRRCSharedMemory), 
                                    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

// Send packet (downlink)
void send_packet(uint8_t dest_id, uint8_t *data, size_t size) {
    CustomApplicationPacket pkt;
    pkt.src_id = my_node_id;
    pkt.dest_id = dest_id;
    pkt.data_size = size;
    memcpy(pkt.data, data, size);
    
    sem_wait(&app_rrc->mutex);
    if (app_rrc->app_to_rrc_count < APP_RRC_QUEUE_SIZE) {
        app_rrc->app_to_rrc_queue[app_rrc->app_to_rrc_back] = pkt;
        app_rrc->app_to_rrc_back = (app_rrc->app_to_rrc_back + 1) % APP_RRC_QUEUE_SIZE;
        app_rrc->app_to_rrc_count++;
    }
    sem_post(&app_rrc->mutex);
}

// Receive packet (uplink)
bool receive_packet(CustomApplicationPacket *pkt) {
    sem_wait(&app_rrc->mutex);
    if (app_rrc->rrc_to_app_count > 0) {
        *pkt = app_rrc->rrc_to_app_queue[app_rrc->rrc_to_app_front];
        app_rrc->rrc_to_app_front = (app_rrc->rrc_to_app_front + 1) % APP_RRC_QUEUE_SIZE;
        app_rrc->rrc_to_app_count--;
        sem_post(&app_rrc->mutex);
        return true;
    }
    sem_post(&app_rrc->mutex);
    return false;
}
```

### PHY Layer Integration

**What PHY needs to do**:
1. Open message queues:
   - Read from `/dev/mqueue/rrc_to_phy`
   - Write to `/dev/mqueue/phy_to_rrc`
2. Periodically send link metrics for active neighbors

**Example PHY Integration**:
```c
// In PHY process
mqd_t phy_from_rrc = mq_open("/rrc_to_phy", O_RDONLY);
mqd_t phy_to_rrc = mq_open("/phy_to_rrc", O_WRONLY);

// Periodically send metrics
void send_metrics(uint8_t node_id, float rssi, float snr, float per) {
    IPC_Message msg;
    msg.type = MSG_PHY_METRICS_UPDATE;
    msg.phy_metrics.node_id = node_id;
    msg.phy_metrics.rssi_dbm = rssi;
    msg.phy_metrics.snr_db = snr;
    msg.phy_metrics.per_percent = per;
    msg.phy_metrics.link_active = true;
    msg.phy_metrics.timestamp = (uint32_t)time(NULL);
    
    mq_send(phy_to_rrc, (char*)&msg, sizeof(IPC_Message), 0);
}
```

---

## Performance Tuning

### Thread Priorities
```c
// Set real-time priority for time-critical threads
pthread_attr_t attr;
struct sched_param param;
pthread_attr_init(&attr);
pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
param.sched_priority = 80;  // High priority
pthread_attr_setschedparam(&attr, &param);
pthread_create(&tdma_handler_thread, &attr, rrc_tdma_message_handler, NULL);
```

### Queue Sizes
Adjust in code:
```c
#define NC_SLOT_QUEUE_SIZE 20      // Increase if overflow
#define APP_RRC_QUEUE_SIZE 16      // Increase for high app traffic
#define RELAY_QUEUE_SIZE 30         // Increase for heavy relay traffic
```

### Message Queue Attributes
Increase message queue capacity:
```c
struct mq_attr attr;
attr.mq_flags = 0;
attr.mq_maxmsg = 20;  // Increase from 10
attr.mq_msgsize = sizeof(IPC_Message);
```

---

## Production Checklist

Before deploying to production:

- [ ] Compile with optimization (`-O2`)
- [ ] Disable assertions (`-DNDEBUG`)
- [ ] Test with all layers integrated
- [ ] Test multi-node scenarios
- [ ] Test route failures and recovery
- [ ] Test high traffic loads
- [ ] Test graceful shutdown under load
- [ ] Monitor for memory leaks (valgrind)
- [ ] Monitor for thread deadlocks
- [ ] Set up automatic restart on crash
- [ ] Configure log rotation
- [ ] Set thread priorities appropriately
- [ ] Tune queue sizes for expected traffic
- [ ] Test signal handling (SIGTERM, SIGINT)
- [ ] Document operational procedures

---

## Next Steps

1. **Compile**: Use provided gcc command
2. **Run**: Execute with node ID
3. **Observe**: Watch loopback test output
4. **Integrate**: Connect OLSR, TDMA, PHY, App layers
5. **Test**: Multi-node network testing
6. **Deploy**: Production deployment with monitoring

**The RRC is ready for integration and testing!**
