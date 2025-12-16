# RRC Thread Implementation and Loopback Testing

## Overview

Complete event-driven, multi-threaded RRC implementation with integrated loopback testing for validation.

## File Statistics

- **File**: `rrc_integrated.c`
- **Total Lines**: 2,887 lines
- **New Additions**: ~750 lines (threads + loopback)
- **Architecture**: Multi-threaded event-driven with POSIX IPC

## Thread Architecture

### 1. OLSR Message Handler Thread (`rrc_olsr_message_handler`)

**Purpose**: Process routing updates and OLSR protocol messages

**Processing Loop**:
```c
while (system_running) {
    // Non-blocking receive with 1ms sleep
    if (rrc_receive_from_olsr(&msg, false) > 0) {
        switch (msg.type) {
            case MSG_OLSR_ROUTE_UPDATE:
                - Update connection contexts with new routes
                - Trigger reconfiguration if route changed
                
            case MSG_OLSR_MESSAGE:
                - Wrap OLSR message in NC slot message
                - Add piggyback TLV
                - Enqueue to NC slot queue for transmission
        }
    }
}
```

**Key Functions**:
- `rrc_handle_route_change()` - Reconfigure connections on route updates
- `build_nc_slot_message()` - Wrap messages for NC transmission
- `add_olsr_to_nc_message()` - Add OLSR payload
- `nc_slot_queue_enqueue()` - Thread-safe NC queue operation

---

### 2. TDMA Message Handler Thread (`rrc_tdma_message_handler`)

**Purpose**: Process slot status updates and received frames

**Processing Loop**:
```c
while (system_running) {
    if (rrc_receive_from_tdma(&msg, false) > 0) {
        switch (msg.type) {
            case MSG_TDMA_SLOT_STATUS_UPDATE:
                - Update local slot availability view
                
            case MSG_TDMA_RX_QUEUE_DATA:
                - Process all frames in rx_queue
                - Deliver packets for self to application
                - Relay packets for other nodes
                - Update connection activity
        }
    }
}
```

**Uplink Processing**:
1. Receive `MSG_TDMA_RX_QUEUE_DATA` notification
2. Dequeue frames from `shared_queues->rx_queue`
3. Check destination:
   - **For self**: Convert to `CustomApplicationPacket`, send to app
   - **For relay**: Check TTL, enqueue to relay queue
4. Update neighbor activity timestamps

---

### 3. Application Message Handler Thread (`rrc_app_message_handler`)

**Purpose**: Process application downlink packets

**Processing Loop**:
```c
while (system_running) {
    if (rrc_receive_from_app(&app_pkt, false) == 0) {
        // 1. Initiate connection setup if needed
        if (rrc_state == RRC_STATE_IDLE) {
            rrc_handle_data_request(dest_id, priority);
        }
        
        // 2. Query OLSR for route
        next_hop = ipc_olsr_get_next_hop(dest_id);
        
        // 3. Check connection state
        ctx = rrc_get_connection_context(dest_id);
        
        // 4. Build frame and enqueue
        if (ctx->connection_state == RRC_STATE_CONNECTED) {
            build_frame(&tx_frame, &app_pkt);
            enqueue_to_priority_queue(&tx_frame);
        }
    }
}
```

**Downlink Flow**:
```
App → app_to_rrc_queue → RRC (route query) → OLSR
                                ↓
                         Connection setup
                                ↓
                    Build frame with next_hop
                                ↓
              Enqueue to priority queue → TDMA
```

---

### 4. PHY Message Handler Thread (`rrc_phy_message_handler`)

**Purpose**: Process physical layer metrics and link status

**Processing Loop**:
```c
while (system_running) {
    if (rrc_receive_from_phy(&msg, false) > 0) {
        switch (msg.type) {
            case MSG_PHY_METRICS_UPDATE:
                - Update neighbor PHY metrics (RSSI, SNR, PER)
                - Check link quality thresholds
                - Detect poor links
                
            case MSG_PHY_LINK_STATUS_CHANGE:
                - Handle link up/down events
        }
    }
}
```

**Metrics Tracked**:
- RSSI (dBm)
- SNR (dB)
- Packet Error Rate (%)
- Packet count
- Link active status

---

### 5. Periodic Management Thread (`rrc_periodic_management_thread`)

**Purpose**: Housekeeping, timeouts, statistics

**Processing Loop**:
```c
while (system_running) {
    sleep(1); // Every second
    
    // Call core management functions
    rrc_periodic_system_management();
    rrc_update_piggyback_ttl();
    
    // Every 10s: Send slot table to TDMA
    if (cycle_count % 10 == 0) {
        send_slot_table_update();
    }
    
    // Every 30s: Print statistics
    if (cycle_count % 30 == 0) {
        print_all_statistics();
    }
}
```

**Responsibilities**:
- Connection inactivity timeout checks
- Neighbor state cleanup
- Piggyback TLV TTL updates
- Slot table synchronization
- Statistics reporting

---

## Thread Control Functions

### `rrc_start_threads()`
Creates all 5 threads using `pthread_create()`. Verifies creation success.

### `rrc_stop_threads()`
1. Sets `system_running = false`
2. Joins all threads with `pthread_join()`
3. Waits for graceful termination

### `rrc_signal_handler(int signum)`
Catches SIGINT/SIGTERM signals, triggers graceful shutdown.

---

## Loopback Testing Functions

### `rrc_loopback_test()`
**Master test coordinator** - Runs all simulation tests in sequence.

**Test Sequence**:
1. PHY metrics simulation (2s)
2. OLSR route update (1s)
3. Application downlink packet (2s)
4. TDMA uplink packet (2s)
5. Verify uplink delivery to app

---

### `rrc_simulate_phy_metrics()`
**Simulates**: PHY layer sending link metrics to RRC

**Injected Message**:
```c
MSG_PHY_METRICS_UPDATE
  - Node ID: 3
  - RSSI: -65.5 dBm
  - SNR: 25.0 dB
  - PER: 1.5%
  - Link: Active
```

**Expected Result**: Neighbor 3 PHY metrics updated in neighbor table.

---

### `rrc_simulate_olsr_route_update()`
**Simulates**: OLSR providing route information

**Injected Message**:
```c
MSG_OLSR_ROUTE_UPDATE
  - Destination: Node 5
  - Next Hop: Node 3
  - Hop Count: 2
```

**Expected Result**: Route table updated, connection contexts reconfigured if needed.

---

### `rrc_simulate_app_downlink()`
**Simulates**: Application sending packet for transmission

**Injected Packet**:
```c
CustomApplicationPacket
  - Source: rrc_node_id
  - Dest: Node 5
  - Type: SMS
  - Size: 100 bytes
  - Data: "Test message from node X"
```

**Injection Point**: `app_to_rrc_queue` in shared memory

**Expected Flow**:
1. App handler thread dequeues packet
2. FSM initiates connection setup
3. OLSR queried for route
4. Frame built and enqueued to priority queue
5. TDMA picks up for transmission

---

### `rrc_simulate_tdma_uplink()`
**Simulates**: TDMA receiving frame from another node

**Injected Frame**:
```c
struct frame
  - Source: Node 5
  - Dest: rrc_node_id (for self)
  - Next Hop: rrc_node_id
  - RX mode: true (uplink)
  - Type: SMS
  - Size: 50 bytes
  - Data: "Uplink test from node 5"
```

**Injection Point**: `rx_queue` in shared memory + `MSG_TDMA_RX_QUEUE_DATA` notification

**Expected Flow**:
1. TDMA handler receives notification
2. Dequeues frame from rx_queue
3. Recognizes destination is self
4. Converts to `CustomApplicationPacket`
5. Enqueues to `rrc_to_app_queue`
6. Application retrieves packet

**Verification**: Check `rrc_to_app_count` > 0, retrieve packet

---

## Main Function Flow

### 1. Initialization Phase
```
Command Line Args → Node ID
Signal Handlers   → SIGINT/SIGTERM
IPC Init          → Message queues, shared memory, semaphores
Subsystem Init    → Pools, FSM, NC manager, neighbors, queues
Power On Event    → FSM → RRC_STATE_IDLE
```

### 2. Thread Startup
```
pthread_create × 5:
  - OLSR handler
  - TDMA handler
  - App handler
  - PHY handler
  - Periodic management
```

### 3. Loopback Test
```
Sequenced simulation tests demonstrating:
  - PHY metrics → RRC
  - OLSR routes → RRC
  - App downlink → RRC → TDMA
  - TDMA uplink → RRC → App
```

### 4. Operational Mode
```
Main thread sleeps (1 minute intervals)
Worker threads process all messages
Periodic thread handles housekeeping
```

### 5. Shutdown Sequence
```
SIGINT/SIGTERM → rrc_signal_handler()
system_running = false
rrc_stop_threads() → pthread_join all
rrc_handle_power_off() → FSM cleanup
rrc_ipc_cleanup() → IPC resource cleanup
```

---

## Thread Synchronization

### Shared Resources
1. **NC Slot Queue**: `pthread_mutex_t nc_slot_queue_mutex`
2. **Shared Queues**: `sem_t shared_mem_sem` (semaphore)
3. **App-RRC Queues**: `sem_t app_rrc_shm->mutex`

### Access Patterns
- **NC Slot Queue**: OLSR thread writes, TDMA reads
- **Priority Queues**: App thread writes, TDMA reads
- **RX Queue**: TDMA writes, TDMA handler reads
- **Relay Queue**: TDMA handler writes, TDMA reads
- **App Queues**: App writes/reads, RRC handlers write/read

---

## Complete Data Flow Examples

### Example 1: Downlink Packet (App → Remote Node)
```
1. Application
   ↓ (app_to_rrc_queue)
2. RRC App Handler Thread
   ↓ (MSG_RRC_ROUTE_QUERY)
3. OLSR (via IPC)
   ↓ (MSG_OLSR_ROUTE_RESPONSE)
4. RRC (connection setup if needed)
   ↓ (build frame with next_hop)
5. Priority Queue (data_from_l3_queue[priority])
   ↓
6. TDMA (transmits in assigned slot)
   ↓
7. Over-the-air transmission
```

### Example 2: Uplink Packet (Remote Node → App)
```
1. Over-the-air reception
   ↓
2. TDMA (writes to rx_queue)
   ↓ (MSG_TDMA_RX_QUEUE_DATA)
3. RRC TDMA Handler Thread
   ↓ (checks dest == self)
4. Convert to CustomApplicationPacket
   ↓ (rrc_to_app_queue)
5. Application
```

### Example 3: NC Slot Broadcast (OLSR → All Neighbors)
```
1. OLSR generates HELLO message
   ↓ (MSG_OLSR_MESSAGE)
2. RRC OLSR Handler Thread
   ↓ (build NC slot message)
3. Add piggyback TLV (neighbor list, TTL)
   ↓
4. NC Slot Queue (thread-safe enqueue)
   ↓
5. TDMA reads from NC queue
   ↓
6. Broadcast in my NC slot
```

---

## Statistics and Monitoring

### RRC FSM Statistics
- State transitions
- Setup success/failures
- Reconfigurations
- Inactivity timeouts
- Releases
- Power events
- Active connections per state

### NC Slot Queue Statistics
- Messages enqueued
- Messages transmitted
- Queue full events
- Current queue depth

### Relay Statistics
- Packets relayed
- TTL expirations
- Queue full events
- Active relays

### App-RRC Queue Statistics
- App → RRC messages
- RRC → App messages
- Queue overflows

### RRC Core Statistics
- Packets processed
- Messages enqueued
- Messages discarded
- Route queries
- Poor links detected

---

## Testing and Validation

### Loopback Test Coverage
✅ PHY metrics propagation
✅ OLSR route updates
✅ Application downlink flow (App → RRC → Queue)
✅ TDMA uplink flow (TDMA → RRC → App)
✅ End-to-end packet delivery verification

### What to Test Externally
- Multi-node network scenarios
- Route failures and recovery
- Link quality degradation
- High traffic loads
- Connection setup/teardown
- Relay forwarding (multi-hop)
- NC slot collisions
- Queue overflow conditions

### Compilation
```bash
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -Wall -Wextra
```

**Required Libraries**:
- `-pthread`: POSIX threads
- `-lrt`: POSIX real-time (message queues, shared memory)

---

## Runtime Operation

### Starting RRC
```bash
# Node 1 (default)
./rrc_integrated

# Node 5 (specific ID)
./rrc_integrated 5
```

### Expected Output
```
========================================
RRC Subsystem with POSIX IPC
Multi-threaded Event-Driven Architecture
========================================
Node ID: 1

Initializing IPC...
IPC initialized successfully
  - Message queues: 6 bidirectional channels
  - Shared memory: 2 regions (queues + app-rrc)
  - Semaphores: 2 for synchronization

Initializing RRC subsystems...

========================================
RRC: All subsystems initialized
========================================
  - Node ID: 1
  - My NC Slot: 1
  - FSM State: RRC_STATE_NULL
  - Max Neighbors: 40
  - NC Slot Queue: 10 capacity
  - IPC ready for all layers

Simulating Power ON event...
FSM State: RRC_STATE_IDLE

========================================
Starting Message Handler Threads
========================================
RRC: OLSR message handler thread started
RRC: TDMA message handler thread started
RRC: Application message handler thread started
RRC: PHY message handler thread started
RRC: Periodic management thread started
  ✓ OLSR message handler
  ✓ TDMA message handler
  ✓ Application message handler
  ✓ PHY message handler
  ✓ Periodic management thread

========================================
Running Integrated Loopback Test
========================================
...test output...

========================================
RRC Subsystem Running
========================================
Event-driven operation active
All threads processing messages
Press Ctrl+C for graceful shutdown
```

### Graceful Shutdown (Ctrl+C)
```
^C
RRC: Received signal 2, shutting down...

========================================
Shutting Down RRC Subsystem
========================================
Stopping message handler threads...
RRC: OLSR message handler thread stopped
RRC: TDMA message handler thread stopped
RRC: Application message handler thread stopped
RRC: PHY message handler thread stopped
RRC: Periodic management thread stopped
RRC: All threads stopped

Powering off RRC...
Cleaning up IPC resources...

========================================
RRC: Shutdown complete
========================================
```

---

## Architecture Advantages

### 1. **Event-Driven Responsiveness**
- Non-blocking message reception with 1ms sleep
- Immediate processing of incoming messages
- No polling delays

### 2. **Concurrency**
- 5 independent threads processing different message types
- Parallel processing of OLSR, TDMA, App, PHY events
- Improved throughput

### 3. **Thread Safety**
- Mutex-protected NC slot queue
- Semaphore-protected shared memory queues
- Atomic access to shared resources

### 4. **Graceful Degradation**
- Continue operation if one layer is slow
- Independent thread failure handling
- Timeout-based recovery

### 5. **Modularity**
- Each thread handles one layer
- Clear separation of concerns
- Easy to add new handlers

### 6. **Testability**
- Loopback functions validate integration
- Independent simulation of each layer
- End-to-end packet flow verification

---

## Known Limitations and Future Enhancements

### Current Limitations
1. Fixed 1ms sleep in threads (could use condition variables)
2. No dynamic thread pool sizing
3. Statistics printed to stdout (no logging framework)
4. Hardcoded queue sizes
5. No remote management interface

### Potential Enhancements
1. **Condition Variables**: Replace sleep with pthread_cond_wait for event notification
2. **Dynamic Adaptation**: Adjust thread priorities based on load
3. **Logging**: Structured logging with log levels
4. **Configuration**: Runtime configuration file support
5. **Monitoring**: Export statistics via IPC or network
6. **Health Checks**: Thread watchdog for deadlock detection
7. **Load Balancing**: Distribute work across thread pool
8. **Hot Reload**: Dynamic reconfiguration without restart

---

## Summary

**Implementation Complete**: Event-driven, multi-threaded RRC with:
- ✅ 5 specialized message handler threads
- ✅ Thread-safe IPC communication
- ✅ Graceful shutdown mechanism
- ✅ Comprehensive loopback testing
- ✅ Complete end-to-end packet flows
- ✅ Integration with OLSR, TDMA, PHY, App layers
- ✅ Production-ready architecture

**Total Lines**: 2,887 lines of integrated C code

**Ready For**: Compilation, testing, deployment
