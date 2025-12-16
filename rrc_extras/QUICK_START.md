# Quick Start Guide - RRC Message Queue System

## 🚀 Get Started in 3 Minutes

### Prerequisites
- GCC compiler with pthread support
- Windows: MinGW or similar
- Linux/macOS: build-essential package

---

## Option 1: Run the Demo (Recommended First Step)

### Windows PowerShell
```powershell
# Navigate to project directory
cd d:\rrcnew10

# Build the demo
.\build_demo.ps1

# Run it
.\rrc_demo.exe
```

### Linux/macOS
```bash
# Navigate to project directory
cd /path/to/rrcnew10

# Make build script executable
chmod +x build_demo.sh

# Build the demo
./build_demo.sh

# Run it
./rrc_demo
```

---

## Option 2: Manual Build

### Windows
```powershell
gcc -o rrc_demo.exe rrc_message_queue.c rrc_api_wrappers.c olsr_thread.c tdma_thread.c phy_thread.c demo_threads.c -lpthread
.\rrc_demo.exe
```

### Linux/macOS
```bash
gcc -o rrc_demo rrc_message_queue.c rrc_api_wrappers.c olsr_thread.c tdma_thread.c phy_thread.c demo_threads.c -lpthread
./rrc_demo
```

---

## What You'll See

```
========================================
RRC Message Queue System Demo
========================================

Initializing message queues...
Message queues initialized

Starting layer threads...
OLSR: Layer thread started for node 1
TDMA: Layer thread started
PHY: Layer thread started
All layer threads started

=== Testing OLSR Communication ===
Test 1: Get next hop for destination 3
OLSR: Route request for destination 3 (req_id=1)
OLSR: Route response sent - next_hop=3 for dest=3
RRC: Next hop for dest 3 = 3

=== Testing TDMA Communication ===
Test 1: Check slot availability (next_hop=2, priority=10)
TDMA: Slot check for next_hop=2 priority=10 -> AVAILABLE
RRC: Slot available = YES

=== Testing PHY Communication ===
Test 1: Get link metrics for node 2
PHY: Metrics request for node 2 -> RSSI=-75.0 SNR=18.5 PER=0.120
RRC: Link metrics - RSSI=-75.0 dBm, SNR=18.5 dB, PER=0.120

=== Message Queue Statistics ===
RRC -> OLSR Queue:
  Enqueued: 2, Dequeued: 2, Overflows: 0
...

Demo completed successfully!
```

---

## Integration with Your Code

### Step 1: Add Headers to rccv2.c
```c
#include <pthread.h>
#include "rrc_message_queue.h"
```

### Step 2: Initialize in init_rrc_fsm()
```c
void init_rrc_fsm(void)
{
    // ... your existing code ...
    
    // Add at the end:
    init_all_message_queues();
    printf("RRC: Message queue system initialized\n");
}
```

### Step 3: Start Layer Threads in main()
```c
int main(void)
{
    init_rrc_fsm();
    
    // Start layer threads
    uint8_t my_node_id = 1;
    pthread_t olsr = start_olsr_thread(my_node_id);
    pthread_t tdma = start_tdma_thread();
    pthread_t phy = start_phy_thread();
    
    // ... rest of your code ...
}
```

### Step 4: Use APIs (No Changes Needed!)
```c
// Your existing code works as-is!
uint8_t next_hop = olsr_get_next_hop(destination);
bool available = tdma_check_slot_available(next_hop, priority);
phy_get_link_metrics(node_id, &rssi, &snr, &per);
```

---

## File Structure

```
rrcnew10/
├── rrc_message_queue.h      ← Message definitions
├── rrc_message_queue.c      ← Queue implementation
├── rrc_api_wrappers.c       ← API wrapper functions
├── olsr_thread.c            ← OLSR layer thread
├── tdma_thread.c            ← TDMA layer thread
├── phy_thread.c             ← PHY layer thread
├── demo_threads.c           ← Demo harness
├── rccv2.c                  ← Your RRC code (modified)
├── README.md                ← Full documentation
├── QUICK_START.md           ← This file
└── build_demo.ps1/sh        ← Build scripts
```

---

## Quick Reference: API Functions

### OLSR APIs
```c
uint8_t olsr_get_next_hop(uint8_t dest);           // Get next hop
void olsr_trigger_route_discovery(uint8_t dest);   // Trigger discovery
```

### TDMA APIs
```c
bool tdma_check_slot_available(uint8_t next_hop, int priority);
bool tdma_request_nc_slot(const uint8_t *payload, size_t len, uint8_t *slot);
```

### PHY APIs
```c
void phy_get_link_metrics(uint8_t node, float *rssi, float *snr, float *per);
bool phy_is_link_active(uint8_t node);
uint32_t phy_get_packet_count(uint8_t node);
```

---

## Troubleshooting

### ❌ "gcc: command not found"
**Solution**: Install GCC compiler
- Windows: Install MinGW from https://www.mingw-w64.org/
- Linux: `sudo apt-get install build-essential`
- macOS: Install Xcode Command Line Tools

### ❌ "pthread: library not found"
**Solution**: Add `-lpthread` flag to gcc command
```bash
gcc ... -lpthread
```

### ❌ Demo runs but shows timeouts
**Solution**: Check that all layer threads started successfully
- Look for "Layer thread started" messages
- Increase timeout values if needed

### ❌ Queue overflow errors
**Solution**: Increase queue size in rrc_message_queue.h
```c
#define MESSAGE_QUEUE_SIZE 64  // Increase from 32
```

---

## Next Steps

1. ✅ Run the demo to verify everything works
2. 📖 Read README.md for detailed documentation
3. 🔧 Integrate with your rccv2.c code
4. 🧪 Test with your application
5. 📊 Monitor queue statistics for performance

---

## Support

- Full Documentation: See `README.md`
- Implementation Details: See `IMPLEMENTATION_SUMMARY.md`
- Code Examples: See `demo_threads.c`

---

## Architecture Quick View

```
Application Layer
       ↓
    [Queue]
       ↓
   RRC Layer ←→ OLSR (L3) via queues
       ↓
    [Queue]
       ↓
   TDMA (L2) ←→ RRC via queues
       ↓
    [Queue]
       ↓
    PHY (L1) ←→ RRC via queues
```

---

**🎉 That's it! You're ready to go!**

Run the demo first, then integrate with your code. The APIs are backward compatible, so minimal changes needed!
