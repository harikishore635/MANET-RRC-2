# RRC POSIX Integration - File Index

## Complete File Listing (16 files)

### Core Header Files (3 files)
```
rrc_posix_mq_defs.h    - Message types, structs, constants, queue/shm names
rrc_shm_pool.h         - Shared memory pool management API
rrc_mq_adapters.h      - POSIX message queue wrapper API
```

### Implementation Files (5 files)
```
rrc_core.c            - Main RRC implementation with TX/RX paths
olsr_daemon.c         - OLSR routing protocol simulator
tdma_daemon.c         - TDMA slot management simulator  
mac_sim.c             - MAC/PHY layer simulator
app_sim.c             - Application layer simulator
```

### Build System (1 file)
```
Makefile              - Build all targets with proper linking
```

### Demo Scripts (4 files)
```
run_demo.sh           - Run single node with all processes
run_all_nodes.sh      - Run 3-node demo in background
stop_demo.sh          - Stop all nodes and cleanup
verify.sh             - Build and test verification script
```

### Documentation (3 files)
```
README.md             - Full documentation (500+ lines)
QUICKSTART.md         - Quick reference guide (300+ lines)
PACKAGE_SUMMARY.md    - Package overview and specs
```

---

## Quick Navigation

**Want to understand the architecture?**
→ Start with `README.md` (Architecture Overview section)

**Want to build and run quickly?**
→ Start with `QUICKSTART.md` (Quick Start section)

**Want to see the implementation?**
→ Start with `rrc_core.c` (main message flows)

**Want to extend or integrate?**
→ Start with `rrc_posix_mq_defs.h` (all message types)

**Want to understand performance?**
→ Start with `PACKAGE_SUMMARY.md` (Technical Specifications)

---

## Implementation Statistics

| Category | Count | Lines |
|----------|-------|-------|
| Headers | 3 | ~850 |
| Source files | 5 | ~1400 |
| Build system | 1 | ~85 |
| Scripts | 4 | ~300 |
| Documentation | 3 | ~1300 |
| **TOTAL** | **16** | **~3935** |

---

## Dependency Graph

```
rrc_core.c
├── rrc_posix_mq_defs.h
├── rrc_shm_pool.h
└── rrc_mq_adapters.h

olsr_daemon.c
├── rrc_posix_mq_defs.h
└── rrc_mq_adapters.h

tdma_daemon.c
├── rrc_posix_mq_defs.h
└── rrc_mq_adapters.h

mac_sim.c
├── rrc_posix_mq_defs.h
├── rrc_shm_pool.h
└── rrc_mq_adapters.h

app_sim.c
├── rrc_posix_mq_defs.h
├── rrc_shm_pool.h
└── rrc_mq_adapters.h
```

---

## Build Order

```
1. make clean          # Clean old artifacts
2. make                # Build all (or use verify.sh)
3. ./run_demo.sh 1     # Test single node
4. ./run_all_nodes.sh  # Test 3-node demo
5. ./stop_demo.sh      # Cleanup
```

---

## Key Files by Purpose

### Learning the Architecture
1. `README.md` - Complete architecture diagrams
2. `PACKAGE_SUMMARY.md` - Technical specs
3. `rrc_posix_mq_defs.h` - Message definitions

### Running the Demo
1. `verify.sh` - Verify build and test
2. `run_demo.sh` - Single node demo
3. `run_all_nodes.sh` - Multi-node demo

### Development Reference
1. `QUICKSTART.md` - API quick reference
2. `rrc_mq_adapters.h` - MQ API
3. `rrc_shm_pool.h` - Pool API

### Understanding Message Flows
1. `rrc_core.c` - TX path (APP→PHY)
2. `rrc_core.c` - RX path (MAC→APP)
3. `README.md` - Message flow diagrams

---

## File Size Distribution

```
Documentation:    ~40%  (README, QUICKSTART, PACKAGE_SUMMARY)
Implementation:   ~35%  (5 .c files)
Headers:          ~20%  (3 .h files)
Scripts:          ~5%   (4 .sh files)
```

---

## Lines of Code by Component

```
Core Logic:       ~2250 lines (headers + source)
Documentation:    ~1300 lines
Build/Scripts:    ~385 lines
────────────────────────────────
TOTAL:            ~3935 lines
```

---

## Checklist for New Users

- [ ] Read `README.md` sections 1-3 (Architecture, Features, Message Flows)
- [ ] Run `./verify.sh` to check build environment
- [ ] Read `QUICKSTART.md` (Message Flow Cheat Sheet)
- [ ] Run `./run_demo.sh 1` for single node test
- [ ] Review `rrc_core.c` handle_app_to_rrc_message() function
- [ ] Run `./run_all_nodes.sh` for multi-node demo
- [ ] Check logs: `tail -f node1.log node2.log node3.log`
- [ ] Review `rrc_posix_mq_defs.h` for message types
- [ ] Experiment with modifying routing table in `olsr_daemon.c`
- [ ] Read `PACKAGE_SUMMARY.md` for integration guide

---

## Contact and Support

For questions about this implementation:
1. Check `README.md` FAQ section
2. Review `QUICKSTART.md` debugging tips
3. Check `PACKAGE_SUMMARY.md` known limitations

---

**Status**: ✅ COMPLETE PACKAGE READY FOR USE

**Last Updated**: 2024

**Total Files**: 16

**Total Lines**: ~3935

**Build Status**: Verified

**Demo Status**: Tested

**Documentation**: Complete
